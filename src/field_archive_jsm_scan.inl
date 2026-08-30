// field_archive_jsm_scan.inl — main JSM script scanner (entity classification).
// Included from field_archive_jsm.inl. Do not compile independently.
//
// v0.16.3 split: this file contains ScanJSMScripts() in full. Two things
// were moved out of the function:
//   1. Cross-pass `static` arrays + their containing struct definitions
//      were hoisted to namespace scope in field_archive_jsm_state.inl.
//      memset() calls below preserve the original zero-on-entry behavior.
//   2. The Director-dispatched interaction detection post-pass was
//      extracted to RunDirectorDetection() in field_archive_jsm_director.inl
//      and replaced here with a single function call.
// All other behavior is byte-for-byte identical to the pre-split source.

// v0.20.49: the scanner helper functions that used to sit here (EntityIsDebugLeftover, AdditemVarByte,
// ReadVarBank, DumpItemGateVars) plus the DumpItemPickupScripts forward decl were extracted VERBATIM to
// field_archive_jsm_scan_helpers.inl to bring this file back under the 80 KB CI hard fail. No behavior
// change; #included right where they were defined so ScanJSMScripts still sees them.
#include "field_archive_jsm_scan_helpers.inl"

bool ScanJSMScripts(const char* fieldName, JSMEntityInfo* outEntities, int maxEntities, int& outCount)
{
    outCount = 0;
    if (!s_initialized) return false;

    // Load the full JSM file.
    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        return false;
    }
    if (jsmData.size() < 8) return false;

    // --- Parse header ---
    // Byte mapping (corrected per deling JsmFile.cpp and NEXT_SESSION_PROMPT research):
    //   byte 0 = countDoors, byte 1 = countLines, byte 2 = countBackgrounds, byte 3 = countOthers
    //   bytes 4-5 = byte offset of script entry point table (uint16 LE)
    //   bytes 6-7 = byte offset of script data section (uint16 LE)
    // NOTE: LoadJSMCounts still uses b1=doors — to be fixed after this is validated.
    int countDoors  = jsmData[0];
    int countLines  = jsmData[1];
    int countBg     = jsmData[2];
    int countOthersH = jsmData[3];  // header's Others count (for logging)

    // Bytes 4-5: byte offset of first entity group entry (uint16 LE).
    // Bytes 6-7: byte offset of script data section (uint16 LE).
    // Entity group table runs from byte 8 to posFirst-1 (but posFirst may equal 8 if no groups).
    // Script entry point table runs from posFirst to posScripts-1.
    // Script data runs from posScripts to EOF.
    uint16_t posFirst   = *(const uint16_t*)(jsmData.data() + 4);  // byte offset of entry point table
    uint16_t posScripts = *(const uint16_t*)(jsmData.data() + 6);  // byte offset of script data

    // Entity group table: bytes 8 to posFirst-1, 2 bytes per entity.
    int totalEntities = ((int)posFirst - 8) / 2;
    if (totalEntities <= 0 || totalEntities > 128) {
        Log::Field("FieldArchive: [JSMScan] bad entity count %d for '%s'", totalEntities, fieldName);
        return false;
    }

    int countOthers = totalEntities - countDoors - countLines - countBg;
    if (countOthers < 0) countOthers = 0;

    Log::Field("FieldArchive: [JSMScan] '%s': %d entities (D=%d L=%d B=%d O=%d hdrO=%d) "
               "posFirst=%d posScripts=%d fileSize=%d",
               fieldName, totalEntities, countDoors, countLines, countBg, countOthers,
               countOthersH,
               (int)posFirst, (int)posScripts, (int)jsmData.size());

    // Validate offsets.
    if (posFirst >= jsmData.size() || posScripts >= jsmData.size() ||
        posScripts <= posFirst) {
        Log::Field("FieldArchive: [JSMScan] invalid offsets for '%s'", fieldName);
        return false;
    }

    // --- Parse entity group table ---
    // Each entry: uint16 LE.
    //   Bits 0-6:  method/script count for this entity (max 127)
    //   Bits 7-15: label = starting index into the script entry point table
    // The label directly encodes the start method index — no cumulative tracking needed.
    // Previous code used bits 0-14 as method count, which made entity 0 swallow all methods.
    struct EntityGroup {
        int methodCount;
        int startMethodIdx;  // index into the script entry point table (from label)
        uint16_t rawEntry;   // for diagnostic logging
    };
    EntityGroup groups[128] = {};
    for (int e = 0; e < totalEntities; e++) {
        uint16_t entry = *(const uint16_t*)(jsmData.data() + 8 + e * 2);
        groups[e].rawEntry       = entry;
        groups[e].methodCount    = entry & 0x7F;         // bits 0-6
        groups[e].startMethodIdx = (int)(entry >> 7);    // bits 7-15 = label
    }

    // v0.58.0: build the engine's own ordering map from the group words. Every
    // category test and SYM lookup below goes through this instead of the old
    // "Door, Line, Bg, Other" index arithmetic, which the exe disagrees with.
    JSMOrderMap order;
    {
        int gs[128];
        for (int e = 0; e < totalEntities; e++) gs[e] = groups[e].startMethodIdx;
        BuildJSMOrderMap(countDoors, countLines, countBg, countOthers,
                         totalEntities, gs, order);
    }
    if (!order.valid) {
        Log::Field("FieldArchive: [JSMORDER] '%s' could not build order map "
                   "(D=%d L=%d B=%d O=%d total=%d) -- scan aborted",
                   fieldName, countDoors, countLines, countBg, countOthers, totalEntities);
        return false;
    }

    // (Entity group boundary diagnostics logged below after SYM names are loaded.)

    // Script entry point table: from posFirst to posScripts-1.
    // Each entry is uint16 LE = dword-index into script data section.
    int entryPointTableSize = (int)(posScripts - posFirst);
    int totalMethods = entryPointTableSize / 2;
    const uint16_t* entryPoints = (const uint16_t*)(jsmData.data() + posFirst);

    // Script data section: array of native uint32 (little-endian on PC!).
    // The PC port already byte-swapped from PS1 big-endian format.
    // Each instruction is 4 bytes. Entry points are dword indices.
    const uint32_t* scriptData = (const uint32_t*)(jsmData.data() + posScripts);
    int scriptDataDwords = (int)(jsmData.size() - posScripts) / 4;

    Log::Field("FieldArchive: [JSMScan] totalMethods=%d scriptDataDwords=%d othersH=%d",
               totalMethods, scriptDataDwords, countOthersH);

    // Confirmed: The PC engine reads raw file bytes as native LE uint32 with NO byte-swap.
    // Opcode = high byte (SHR word,24). Param = low 24 bits. High byte 0 = push literal.
    // Opcodes > 0xFF use a two-stage dispatch via prefix opcode 0x1C.
    // Diagnostic: opcode frequency histogram to validate against runtime OPCODE-HIST.
    {
        int opcodeHist[0x100] = {};  // primary opcodes are 0x00-0xFF
        int totalOpcodes = 0, totalPushes = 0;
        for (int d = 0; d < scriptDataDwords; d++) {
            uint32_t w = scriptData[d];  // native LE read
            uint8_t highByte = (uint8_t)(w >> 24);
            if (highByte == 0) {
                totalPushes++;
            } else {
                opcodeHist[highByte]++;
                totalOpcodes++;
            }
        }
        Log::Field("FieldArchive: [JSMScan] stats: %d opcodes, %d pushes out of %d",
                   totalOpcodes, totalPushes, scriptDataDwords);
        char histBuf[2048] = {};
        int hp = 0;
        for (int i = 0; i < 0x100 && hp < 1900; i++) {
            if (opcodeHist[i] > 0)
                hp += snprintf(histBuf + hp, 2048 - hp, "%02X=%d ", i, opcodeHist[i]);
        }
        if (hp > 0)
            Log::Field("FieldArchive: [JSMScan] opcodes: %s", histBuf);
    }

    // --- Also load SYM names for cross-reference ---
    char symNames[128][32] = {};
    int symCount = 0;
    LoadSYMNames(fieldName, symNames, 128, symCount);

    // v0.58.0: the v0.20.26 SYMREMAP heuristic (categorize each SYM name by
    // whether it declares an "across"/"talk"/"push" method, then remap per
    // category) is gone. It guessed, and on the three biggest Esthar fields --
    // ecenter1, ecmway1, ecoway1 -- it guessed wrong, fell back to
    // `symIdx = e - countDoors`, and put every single name on the wrong entity.
    // JSMOrderMap::symIdx is derived from the group words themselves and needs
    // no heuristic. Logged so a BAT can still see the mapping.
    Log::Field("FieldArchive: [JSMORDER] '%s' D=%d L=%d B=%d O=%d -- group array is "
               "L,D,B,O; runtime table is O,L,B,D; SYM bare list is O,L,B (no doors) "
               "[v0.58.0 exe 0x0052BC00]",
               fieldName, countDoors, countLines, countBg, countOthers);

    // Diagnostic: log entity group boundaries with SYM names.
    for (int e = 0; e < totalEntities; e++) {
        int symIdx = order.symIdx[e];
        const char* sym = (symIdx >= 0 && symIdx < symCount) ? symNames[symIdx] : "(door)";
        static const char* kCat[4] = { "Door", "Line", "Bg", "Other" };
        if (e < 15 || groups[e].methodCount > 0) {
            Log::Field("FieldArchive: [JSMScan] grp[%d] raw=0x%04X methods=%d startIdx=%d "
                       "cat=%s slot=%d sym='%s'",
                       e, (unsigned)groups[e].rawEntry, groups[e].methodCount,
                       groups[e].startMethodIdx,
                       (order.cat[e] >= 0 && order.cat[e] < 4) ? kCat[order.cat[e]] : "?",
                       (int)order.slot[e], sym);
        }
    }

    // v0.16.3: Reset cross-pass state arrays declared in field_archive_jsm_state.inl.
    // These were previously function-local `static`; hoisting them to namespace
    // scope preserves the same one-time zero-initialization at program start,
    // and the explicit memset below preserves the zero-on-entry contract for
    // every subsequent call.
    memset(s_methodMapjumps,    0, sizeof(s_methodMapjumps));
    memset(s_entityReqs,        0, sizeof(s_entityReqs));
    memset(s_entityPopms,       0, sizeof(s_entityPopms));
    memset(s_hasSetmodelInit,   0, sizeof(s_hasSetmodelInit));
    memset(s_hasDialogAny,      0, sizeof(s_hasDialogAny));
    memset(s_hasExtDispatchArr, 0, sizeof(s_hasExtDispatchArr));
    memset(s_initVarMaps,       0, sizeof(s_initVarMaps));
    memset(s_reqOpcodeCount,    0, sizeof(s_reqOpcodeCount));
    memset(s_isReqTarget,       0, sizeof(s_isReqTarget));  // v0.19.7 (#5): field-wide REQ-target set
    memset(s_lineInitOff,       0, sizeof(s_lineInitOff));  // v0.62.0 (#123)

    // --- Scan each entity ---
    for (int e = 0; e < totalEntities && outCount < maxEntities; e++) {
        JSMEntityInfo& info = outEntities[outCount];
        memset(&info, 0, sizeof(info));
        info.jsmIndex = e;
        info.param = -1;
        info.modelParam = -1;   // v0.62.0: no SETMODEL seen yet
        info.hasGate    = false;  // v0.62.2
        info.exitFromReqFollow = false;  // v0.63.0

        // v0.58.0: category and SYM index both come from JSMOrderMap, which is
        // read out of field_scripts_init rather than assumed. The group array is
        // Lines, Doors, Backgrounds, Others -- the old code had Door and Line the
        // wrong way round, so on every field carrying both (79 of them) real
        // trigger Lines were typed JSM_ENT_DOOR and never scanned as lines.
        int catStart = 0;
        info.jsmCategory = order.cat[e];
        switch (info.jsmCategory) {
            case 0:  info.type = JSM_ENT_DOOR; break;
            case 1:  info.type = JSM_ENT_LINE_TRIGGER; break;
            case 2:  info.type = JSM_ENT_BACKGROUND; break;
            default: info.type = JSM_ENT_UNKNOWN; break;  // Other: classified by opcodes
        }
        info.runtimeSlot = (e >= 0 && e < JSM_ORDER_MAX) ? (int)order.slot[e] : -1;

        int symIdx = order.symIdx[e];
        if (symIdx >= 0 && symIdx < symCount) {
            strncpy(info.symName, symNames[symIdx], 31);
            info.symName[31] = '\0';
        }

        // v0.20.26: camera-view transition lines follow FF8's "*jump*" naming
        // (validated across all 45 multi-camera fields). Log with corrected names.
        if (info.jsmCategory == 1 && info.symName[0] &&
            (strstr(info.symName, "jump") || strstr(info.symName, "Jump"))) {
            Log::Field("FieldArchive: [CAMXLINE] '%s' line ent%d '%s' = camera-view transition [v0.20.26]",
                       fieldName, e, info.symName);
        }

        // --- Scan this entity's scripts for signature opcodes ---
        // We scan ALL methods (init + interaction scripts).
        // Track push stack for extracting parameters (last N pushes before an opcode).
        static const int PUSH_STACK_MAX = 8;
        int32_t pushStack[PUSH_STACK_MAX] = {};
        int pushCount = 0;

        bool foundSetDrawpoint = false;
        bool foundDrawpoint    = false;
        bool foundMenusave     = false;
        bool foundSaveenable   = false;
        // v0.17.8.8: literal PUSH of the save-enable opcode constants (NOT via a
        // resolvable 0x1C). A save LINE like bghall_1 'selphie' pushes 0x12F/0x130
        // for a runtime-supplied 0x1C, so the opcode-resolved flags above never
        // catch it. Consumed only by the line-scoped signal-(a) below.
        bool sawLitMenusave    = false;
        bool sawLitSaveenable  = false;
        bool sawLitPhsenable   = false;
        bool foundMenushop     = false;
        bool foundCardgame     = false;
        bool foundLadder       = false;
        bool foundMapjump      = false;
        bool foundSetmodel     = false;
        bool foundSetmodelInit = false;  // v0.12.20: SETMODEL specifically in method 0 (init)
        bool foundTalkon       = false;
        bool foundTalkradius   = false;  // v0.17.7.1: TALKRADIUS opcode in this entity's scripts
        // v0.75.0 (#112): the SAME two opcodes, but scoped to the INIT method.
        // "Hides itself somewhere" and "hides itself as it sets up, and enables
        // talking in the same breath" are different objects. The second is an
        // invisible interaction point -- a terminal, a panel, a sign whose
        // picture is in the background art. See JSMEntityInfo::invisibleTalkTarget.
        bool hideInInit        = false;
        bool talkOnInInit      = false;
        bool foundHide         = false;  // v0.19.4 diag: entity's own script HIDEs itself (0x061) -- a pickup self-hides on collect; a real/silent NPC does not. Log-only, reported in [MODELSIG].
        bool foundTalkRad62    = false;  // v0.19.8 RE: real TALKRADIUS (0x62) -> entity talk radius 0x1F8
        bool foundPushRad63    = false;  // v0.19.8 RE: real PUSHRADIUS (0x63) -> entity push radius 0x1F6
        bool foundProxChk5B    = false;  // v0.19.8 RE: opcode 0x5B tests "is target within talkRad+pushRad"
        bool foundNonInitVarWrite = false;  // v0.19.5: POPM (savemap write) in a NON-init method -- the pickup's "collected" flag write (Urakata var304, saveline0 var450, both method[2]).
        bool sawLitAdditem     = false;  // v0.19.5: literal push of ADDITEM (0x125) -- inventory item-pickup grant (exe-confirmed: 0x125 pops item id+count, calls inventory-add 0x47ED00).
        bool foundDoorline     = false;
        bool foundParticleon   = false;
        bool foundAdditem      = false;
        // v0.59.0: the self-gating signature of a collectible -- its init method
        // READS a savemap variable (to decide whether it has already been taken)
        // and one of its interaction methods WRITES that same variable. A story NPC
        // that sets a "talked to" flag writes one but never reads it back in init,
        // and the old test (any non-init write at all) could not tell them apart:
        // it flagged 876 entities across 400 fields as collectibles, Raijin, Fuujin
        // and half the Balamb townsfolk among them.
        static const int MAX_SELFGATE = 24;
        int16_t initReadVars[MAX_SELFGATE];  int nInitReadVars  = 0;
        int16_t nonInitWriteVars[MAX_SELFGATE]; int nNonInitWriteVars = 0;
        int  mapjumpDestField  = -1;
        int  drawpointId       = -1;
        int  shopId            = -1;

        // v0.07.82: Camera/scroll/event flags for Line entity classification.
        bool foundBgdraw       = false;  // BGDRAW or BGOFF
        bool foundScroll       = false;  // any DSCROLL/LSCROLL/CSCROLL/SETCAMERA
        bool foundEventOp      = false;  // SHOW/HIDE/USE/UNUSE/MES/ASK/BATTLE/MOVE/REQ
        bool foundDialogOp     = false;  // v0.07.98: MES/ASK/AMES/AASK specifically (for interactive object detection)
        // v0.59.0: opcode 0x1C is a two-instruction no-op (0x0051D710), not a prefix
        // for extended dispatch -- opcodes above 0xFF are encoded directly as bare
        // words. It occurs in 92.8% of all entities on the disc, so the old flag was
        // very nearly a constant, and every consumer used it as `hasDialogAny ||
        // hasExtDispatch` -- a fallback for "this entity might talk". With the decode
        // corrected, MES/ASK/AMES/AASK are visible directly and hasDialogAny is the
        // real answer. Kept as a permanent false so the field, the struct and the log
        // lines stay put rather than rippling a removal through six files.
        const bool foundExtDispatch = false;
        bool foundBattle       = false;  // BATTLE specifically
        // v0.120.0 (#centra): the BTNTEST mask this entity waits on, 0 = none.
        // First one wins; the init method is excluded because a BTNTEST there
        // is not the thing the player has to press when they arrive.
        uint16_t touchBtnMask  = 0;
        bool foundSetline     = false;  // SETLINE interaction zone
        int16_t setlineX1 = 0, setlineY1 = 0, setlineZ1 = 0;
        int16_t setlineX2 = 0, setlineY2 = 0, setlineZ2 = 0;

        // Each entity occupies methodCount + 1 method slots in the entry point table.
        // The group entry's count omits the init script (method 0), so we loop
        // from 0 to methodCount INCLUSIVE to cover all methods.
        for (int m = 0; m <= groups[e].methodCount; m++) {
            int methodIdx = groups[e].startMethodIdx + m;
            if (methodIdx >= totalMethods) break;

            uint16_t scriptStart = entryPoints[methodIdx] & 0x7FFF;  // v0.12.23: mask off bit15 flag (set on Door/Line/BG entities)

            // Find the end of this method: either the start of the next method,
            // or the end of the script data.
            uint16_t scriptEnd = (uint16_t)scriptDataDwords;
            if (methodIdx + 1 < totalMethods)
                scriptEnd = entryPoints[methodIdx + 1] & 0x7FFF;  // v0.12.23: mask bit15 here too

            // Reset push stack for each method.
            pushCount = 0;

            // v0.07.84: Per-method MAPJUMP tracking for REQ-following.
            bool methodHasMapjump = false;
            int  methodMapjumpDest = -1;
            int  methodMapjumpRel  = -1;   // v0.62.3: index of the jump within the method

            // v0.07.87: Per-method PSHM_W address tracking for variable-dispatch.
            int32_t methodPshmAddrs[MAX_PSHM_PER_METHOD] = {};
            int methodPshmCount = 0;

            // v0.18.3.295 (#85): state-guard capture, per method.
            // Look for the leading `PSHM_L <addr>` + `PSHM_W <value>` pair -- the
            // shape a script uses to test a field variable before deciding whether
            // to place itself. Recorded onto the entity only if this method goes on
            // to call SET3, so it describes the condition under which the entity
            // actually exists in the world.
            //
            // FIRST pair wins, not the most recent: a compound guard tests several
            // variables and the leading one is the discriminator. glwater3's
            // ladline5 reads `340 vs 9` then `339 vs 64`; the second is true in the
            // OTHER world state, so taking the last pair would invert the answer.
            bool    guardSeen = false;
            int16_t guardAddr = 0, guardVal = 0;
            bool    prevWasPshmL = false;
            int32_t prevPshmLAddr = 0;
            // v0.18.3.296 (#85): did a JPF (jump-if-false, opcode 0x02) execute
            // between the guard and the SET3? That is what makes the placement
            // genuinely CONDITIONAL. See the state-guard comment in field_archive.h
            // -- an unconditional SET3 preceded by a guard read is a state QUERY
            // used for something else, not a precondition for existing.
            bool    guardHadJpf = false;

            for (int ip = (int)scriptStart; ip < (int)scriptEnd && ip < scriptDataDwords; ip++) {
                uint32_t word = scriptData[ip];  // native LE read of raw file bytes
                uint8_t highByte = (uint8_t)(word >> 24);

                // v0.07.75: SVDUMP diagnostic logging disabled — position extraction confirmed.
                bool detailDump = false;

                // ------------------------------------------------------------------
                // v0.59.0: the engine's own decoder, from 0x00530760.
                //
                //   w = code[ip]
                //   if ((w & 0xFF000000) == 0)  opcode = w         ; NO parameter
                //   else                        opcode = w >> 24   ; param = sext24(w)
                //   call opcodeTable[opcode]                       ; table @ 0x00B8DE94
                //
                // A word whose high byte is zero is an OPCODE, not a literal push --
                // that is simply how opcodes above 0xFF are encoded (MENUSAVE 0x12E,
                // ADDITEM 0x125, SETDRAWPOINT 0x155, DRAWPOINT 0x137, ...). Reading
                // them as literal pushes is what made the old "0x1C pops the extended
                // opcode off the stack" model necessary; opcode 0x1C is in fact a
                // two-instruction no-op (0x0051D710) and appears in 92.8% of all
                // entities, so hasExtDispatch was very nearly a constant.
                //
                // Literals reach the VM stack only through a push opcode. Walking every
                // handler in the table and counting writes to the stack pointer at
                // [ctx+0x184] gives the complete push set:
                //   0x07 PSHN_L 0x0051C990  pushes the INLINE PARAM        -- literal
                //   0x13        0x0051CD30  pushes the INLINE PARAM        -- literal
                //   0x08 PSHL   0x0051CAB0  local  [ctx + n*4 + 0x140]
                //   0x0A/0x0C/0x0E          field var bank 0x01CFE9B8 (byte/word/long)
                //   0x10 0x11 0x12          push, value not statically known
                //   0x04 CALL   0x0051C530  pushes the return IP    -- NOT an argument
                //   0x05        0x0051C570  pushes all EIGHT locals -- NOT an argument
                //
                // The previous model had 0x07 as PSHM_W and turned every NON-NEGATIVE
                // param into a fake "from memory" marker. 8289 of the 8625 SET3
                // placements on the disc have at least one non-negative coordinate, so
                // 96% of every entity position in the game was discarded as unresolvable
                // -- which is what the triangle-centroid, late-resolve and struct-scan
                // fallbacks have all been compensating for. Reading it correctly puts
                // 4539 of 4593 entity positions (98.8%) strictly INSIDE the walkmesh
                // triangle their own SET3 names, which is an independent check: the
                // triangle is the opcode's inline param and the coordinates are popped
                // values, so they agree only if both are read right.
                //
                // ARGUMENTS come from the contiguous run of push opcodes immediately
                // before the consumer -- the shape the compiler emits -- so the stack is
                // cleared after every non-push opcode below and pushStack[pushCount-N]
                // is correct by construction rather than by luck.
                // ------------------------------------------------------------------
                const JsmInsn insn = JsmDecodeWord(word);
                const uint16_t opcode   = insn.opcode;
                const int32_t  opcParam = insn.param;
                const bool     bareWord = insn.bare;

                if (detailDump) {
                    Log::Field("FieldArchive: [SVDUMP] ent=%d m=%d ip=%d OP 0x%03X param=%d bare=%d stk=%d word=0x%08X",
                               e, m, ip, (int)opcode, (int)opcParam, bareWord ? 1 : 0, pushCount, word);
                }

                // --- push opcodes: extend the current argument run -------------------
                if (JsmIsArgPush(insn)) {
                    const int32_t v = JsmIsLiteralPush(insn) ? opcParam
                                                             : JsmRuntimeMarker(opcParam);
                    if (pushCount < PUSH_STACK_MAX) {
                        pushStack[pushCount++] = v;
                    } else {
                        for (int sft = 0; sft < PUSH_STACK_MAX - 1; sft++)
                            pushStack[sft] = pushStack[sft + 1];
                        pushStack[PUSH_STACK_MAX - 1] = v;
                    }
                    // v0.07.87: record variable-bank reads for variable-dispatch
                    // detection. These are the REAL memory reads (0x0A/0x0C/0x0E),
                    // not 0x07 -- which is a literal and never touched memory.
                    if (opcode == 0x0A || opcode == 0x0C || opcode == 0x0E) {
                        if (methodPshmCount < MAX_PSHM_PER_METHOD) {
                            bool dupR = false;
                            for (int d = 0; d < methodPshmCount; d++)
                                if (methodPshmAddrs[d] == opcParam) { dupR = true; break; }
                            if (!dupR) methodPshmAddrs[methodPshmCount++] = opcParam;
                        }
                        // v0.59.0: init-method reads, for the self-gating test.
                        if (m <= 0 && nInitReadVars < MAX_SELFGATE) {
                            bool dupI = false;
                            for (int d = 0; d < nInitReadVars; d++)
                                if (initReadVars[d] == (int16_t)opcParam) { dupI = true; break; }
                            if (!dupI) initReadVars[nInitReadVars++] = (int16_t)opcParam;
                        }
                    }
                    continue;
                }
                // CALL's return address and the prologue's saved locals are pushes,
                // but they are never arguments -- they end the run instead.
                if (JsmIsNonArgPush(insn)) { pushCount = 0; continue; }

                // v0.17.8.8 kept: these used to be spotted as "literal pushes" of the
                // opcode number, which under the corrected decode is the opcode itself.
                // Downstream save-line logic reads the sawLit* flags, so keep feeding them.
                if (opcode == JSM_OP_MENUSAVE)        sawLitMenusave   = true;
                else if (opcode == JSM_OP_SAVEENABLE) sawLitSaveenable = true;
                else if (opcode == JSM_OP_PHSENABLE)  sawLitPhsenable  = true;
                else if (opcode == JSM_OP_ADDITEM)    sawLitAdditem    = true;

                // v0.59.0: savemap writes. The engine's pop-to-variable-bank opcodes
                // are 0x0B POPM_B (0x0051CCA0), 0x0D POPM_W (0x0051CCD0) and 0x0F
                // POPM_L (0x0051CD00) -- each stores the popped value at
                // [param + 0x01CFE9B8], the field variable bank. The previous code
                // watched 0x08 and 0x0B; 0x08 is PSHL, a PUSH of a local, so half the
                // var-write record was reading the wrong opcode and the other half was
                // missing POPM_W and POPM_L entirely. foundNonInitVarWrite is one of the
                // item-pickup signals, so this fed straight into NPC-vs-Item.
                if (JsmIsVarBankPop(insn)) {
                    if (e < 128 && pushCount > 0 &&
                        ((uint32_t)pushStack[pushCount-1] & 0x80000000u) == 0 &&
                        s_initVarMaps[e].count < 64) {
                        s_initVarMaps[e].writes[s_initVarMaps[e].count].addr  = opcParam;
                        s_initVarMaps[e].writes[s_initVarMaps[e].count].value = pushStack[pushCount-1];
                        s_initVarMaps[e].count++;
                    }
                    if (e < 128 && s_entityPopms[e].count < MAX_POPM_PER_ENTITY) {
                        bool dupW = false;
                        for (int d = 0; d < s_entityPopms[e].count; d++)
                            if (s_entityPopms[e].addrs[d] == opcParam) { dupW = true; break; }
                        if (!dupW) s_entityPopms[e].addrs[s_entityPopms[e].count++] = opcParam;
                    }
                    // v0.19.5: a savemap write from a NON-init method to a real
                    // variable is the collectible's "already picked up" flag.
                    if (m >= 1 && opcParam >= 8) {
                        foundNonInitVarWrite = true;
                        if (nNonInitWriteVars < MAX_SELFGATE) {
                            bool dupW2 = false;
                            for (int d = 0; d < nNonInitWriteVars; d++)
                                if (nonInitWriteVars[d] == (int16_t)opcParam) { dupW2 = true; break; }
                            if (!dupW2) nonInitWriteVars[nNonInitWriteVars++] = (int16_t)opcParam;
                        }
                    }
                }

                // v0.18.3.295 (#85): track the PSHM_L -> PSHM_W adjacency that forms
                // a state guard. 0x0A = PSHM_L (push long from field varblock),
                // 0x07 = PSHM_W (here supplying the literal compare value).
                if (highByte == 0x02 && guardSeen) guardHadJpf = true;   // v0.18.3.296: JPF
                if (highByte == 0x0A) {
                    prevWasPshmL  = true;
                    prevPshmLAddr = opcParam;
                } else {
                    if (prevWasPshmL && highByte == 0x07 && !guardSeen &&
                        prevPshmLAddr >= 0 && prevPshmLAddr < 0x2000) {
                        guardSeen = true;
                        guardAddr = (int16_t)prevPshmLAddr;
                        guardVal  = (int16_t)opcParam;
                    }
                    prevWasPshmL = false;
                }

                // --- Check for signature opcodes ---

                // SET3: position from init script (method 0), or from any later
                // method if init has none.
                // Primary: 4 stack params (X, Y, Z, triangleId) — works for literal pushes.
                // Fallback: 3 stack params (X, Y, Z) + triangle from opcParam.
                // The fallback handles entities that use PSHM_W for coordinates
                // (e.g. bggate_2 dp01: X/Y/Z from PSHM_W markers, tri=194 in opcParam).
                // v0.07.75: Added 3-param fallback for draw point position extraction.
                // v0.07.99: PSHM_W marker detection — when coordinates come from runtime
                // memory, store the memory addresses instead of garbage position values.
                // v0.18.3.286 (#85): dropped the `m == 0` restriction. Offline sewer-field
                // survey found several gate entities (glwater4/glwater5 sakuN) call SET3
                // in a later, REQ-triggered toggle method instead of init -- their own
                // init script never places them at all. The `!info.hasPosition &&
                // !info.hasPshmCoords` guards already make this "first SET3 found wins,
                // method 0 first" (the loop walks m=0,1,2... in order), so this is purely
                // additive: entities that already resolved from method 0 are unaffected;
                // only entities that previously got NO position from method 0 can now
                // pick one up from a later method. Risk: a later method's SET3 could be a
                // transient/cutscene reposition rather than the entity's "home" position,
                // for some entity elsewhere in the game that happens to have no init-method
                // SET3 today -- watch for a newly-appearing but implausible position on
                // BAT if this surfaces something unexpected outside the sewer fields.
                if (opcode == JSM_OP_SET3 && !info.hasPosition && !info.hasPshmCoords) {
                    // Check which stack values (if any) are PSHM_W markers.
                    // v0.08.00: Markers use bit 31 (0x8000xxxx). Literal pushes max at 0x00FFFFFF.
                    int coordBase = -1;  // index of X in pushStack
                    int paramCount = 0;  // 3 or 4 params found
                    if (pushCount >= 4) {
                        coordBase = pushCount - 4;
                        paramCount = 4;
                    } else if (pushCount >= 3 && opcParam >= 0 && opcParam < 4096) {
                        coordBase = pushCount - 3;
                        paramCount = 3;
                    }
                    if (coordBase >= 0) {
                        // Check if X, Y, or Z values are PSHM_W markers.
                        // v0.08.13: Tightened marker detection. PSHM markers are
                        // exactly 0x8000xxxx (bit31 set, bits 16-30 all zero).
                        // Negative passthrough literals (e.g. -82 = 0xFFFFFFAE)
                        // also have bit31 set but have bits 16-30 set too.
                        // The mask 0xFFFF0000 == 0x80000000 catches only markers.
                        bool xIsPshm = ((uint32_t)pushStack[coordBase + 0] & 0xFFFF0000u) == 0x80000000u;
                        bool yIsPshm = ((uint32_t)pushStack[coordBase + 1] & 0xFFFF0000u) == 0x80000000u;
                        bool zIsPshm = ((uint32_t)pushStack[coordBase + 2] & 0xFFFF0000u) == 0x80000000u;
                        bool anyPshm = xIsPshm || yIsPshm || zIsPshm;
                        if (anyPshm) {
                            // v0.07.99: Coordinates come from runtime memory.
                            // Store the memory addresses for runtime resolution.
                            info.hasPshmCoords = true;
                            info.pshmAddrX = xIsPshm ? (int16_t)(pushStack[coordBase + 0] & 0xFFFF) : 0;
                            info.pshmAddrY = yIsPshm ? (int16_t)(pushStack[coordBase + 1] & 0xFFFF) : 0;
                            info.pshmAddrZ = zIsPshm ? (int16_t)(pushStack[coordBase + 2] & 0xFFFF) : 0;
                            // Log the full stack dump for diagnostic.
                            char stkBuf[256] = {};
                            int bp = 0;
                            for (int si = 0; si < pushCount && bp < 240; si++)
                                bp += snprintf(stkBuf + bp, 256 - bp, "0x%08X ", (unsigned)pushStack[si]);
                            // v0.18.3.287 (#85): the 4-param case's triangle slot
                            // (pushStack[coordBase+3]) can ITSELF be a PSHM_W marker,
                            // not a literal -- casting straight to uint16_t silently
                            // extracts the marker's runtime memory ADDRESS as if it
                            // were a walkmesh triangle number. Confirmed on glwater3:
                            // ladline5/ladline6/saku2/saku3 all share PSHM address
                            // 0x3A (58 decimal) in that slot -- coincidentally a VALID
                            // triangle index for that field, so all four silently got
                            // the SAME wrong shared position (Aaron's BAT: "three steps
                            // away" while standing at the actual gate). The real,
                            // per-entity triangle is available from opcParam (the SET3
                            // instruction's own embedded immediate, independent of the
                            // stack) whenever it's in a plausible walkmesh range --
                            // verified against the real field archive: ladline5=186,
                            // ladline6=175, saku2=181, saku3=54, all distinct and each
                            // landing in a sensible, spread-out position instead of the
                            // one shared bogus spot.
                            bool triSlotIsMarker = (paramCount == 4) &&
                                (((uint32_t)pushStack[coordBase + 3] & 0xFFFF0000u) == 0x80000000u);
                            uint16_t tri;
                            if (triSlotIsMarker) {
                                tri = (opcParam >= 0 && opcParam < 4096) ? (uint16_t)opcParam : 0;
                            } else {
                                tri = (paramCount == 4) ? (uint16_t)pushStack[coordBase + 3] : (uint16_t)opcParam;
                            }
                            Log::Field("FieldArchive: [SET3-DIAG] ent%d '%s' PSHM_W coords: "
                                       "X=%s(addr=%d) Y=%s(addr=%d) Z=%s(addr=%d) tri=%u%s "
                                       "stack[%d]=[%s]",
                                       e, info.symName,
                                       xIsPshm ? "PSHM" : "lit", (int)info.pshmAddrX,
                                       yIsPshm ? "PSHM" : "lit", (int)info.pshmAddrY,
                                       zIsPshm ? "PSHM" : "lit", (int)info.pshmAddrZ,
                                       (unsigned)tri, triSlotIsMarker ? " [tri-slot was PSHM marker, used opcParam]" : "",
                                       pushCount, stkBuf);
                            // Also store the triangle even though position is runtime.
                            info.posTriangle = tri;
                            // Store literal values for any non-PSHM coordinates.
                            if (!xIsPshm) info.posX = (int16_t)pushStack[coordBase + 0];
                            if (!yIsPshm) info.posY = (int16_t)pushStack[coordBase + 1];
                            if (!zIsPshm) info.posZ = (int16_t)pushStack[coordBase + 2];

                            // v0.08.13: Shift-pattern position promotion.
                            // When the first PSHM_W param (X) is unresolved but the
                            // second (Y) and third (Z) are resolved passthrough literals,
                            // the entity uses the shift pattern: the first param is a
                            // mode selector or entity-scope index consumed by the engine,
                            // and the actual navigable position is (litY, litZ).
                            // Confirmed by runtime data: l1 (1032,-2865,-5421)->pos(-2865,-5421),
                            // stairlight (1,-700,-8593)->pos(-700,-8593).
                            // Safety: only apply when litY AND litZ are both non-zero
                            // (avoids false positives like elelight where only litY is set).
                            if (xIsPshm && !yIsPshm && !zIsPshm &&
                                info.posY != 0 && info.posZ != 0) {
                                info.posX = info.posY;   // litY -> navigable X
                                info.posY = info.posZ;   // litZ -> navigable Y
                                info.posZ = 0;           // no Z for 2D nav
                                info.hasPosition = true;
                                Log::Field("FieldArchive: [SET3-SHIFT] ent%d '%s' shift-pattern: "
                                           "pos=(%d,%d) from litY/litZ passthrough",
                                           e, info.symName,
                                           (int)info.posX, (int)info.posY);
                            }
                        } else {
                            // All literal values — extract normally.
                            info.posX = (int16_t)pushStack[coordBase + 0];
                            info.posY = (int16_t)pushStack[coordBase + 1];
                            info.posZ = (int16_t)pushStack[coordBase + 2];
                            info.posTriangle = (paramCount == 4)
                                ? (uint16_t)pushStack[coordBase + 3]
                                : (uint16_t)opcParam;
                            info.hasPosition = true;
                        }

                        // v0.18.3.295 (#85): this method placed the entity, so any
                        // state guard leading it is the condition under which the
                        // entity exists. Record it (capture only -- acting on it is
                        // the catalog's mutual-exclusion pass, which fires only when
                        // 2+ entities on the field share an address with DIFFERENT
                        // values, so a lone unrelated guard changes nothing).
                        if (guardSeen && !info.hasStateGuard) {
                            info.hasStateGuard = true;
                            info.stateVarAddr  = guardAddr;
                            info.stateVarValue = guardVal;
                            info.stateGuardConditional = guardHadJpf;
                            Log::Field("FieldArchive: [STATE-GUARD] ent%d '%s' SET3 gated on "
                                       "varblock[0x%04X] (%d) == %d, conditional=%d (JPF %s) [v0.18.3.296]",
                                       e, info.symName, (unsigned)(uint16_t)guardAddr,
                                       (int)guardAddr, (int)guardVal, guardHadJpf ? 1 : 0,
                                       guardHadJpf ? "present -- placement is state-dependent"
                                                   : "ABSENT -- SET3 is unconditional, entity always exists");
                        }
                    }
                }

                // SET: 2D position from init script (method 0), or from any later
                // method if init has none (v0.18.3.286 (#85), same reasoning as SET3 above).
                // Primary: 3 stack params (X, Y, triangleId).
                // Fallback: 2 stack params (X, Y) + triangle from opcParam.
                if (opcode == JSM_OP_SET && !info.hasPosition) {
                    if (pushCount >= 3) {
                        info.posX = (int16_t)pushStack[pushCount - 3];
                        info.posY = (int16_t)pushStack[pushCount - 2];
                        info.posZ = 0;
                        info.posTriangle = (uint16_t)pushStack[pushCount - 1];
                        info.hasPosition = true;
                    } else if (pushCount >= 2 && opcParam >= 0 && opcParam < 4096) {
                        info.posX = (int16_t)pushStack[pushCount - 2];
                        info.posY = (int16_t)pushStack[pushCount - 1];
                        info.posZ = 0;
                        info.posTriangle = (uint16_t)opcParam;
                        info.hasPosition = true;
                    }
                }

                // SETDRAWPOINT: stack param = draw point ID.
                if (opcode == JSM_OP_SETDRAWPOINT) {
                    foundSetDrawpoint = true;
                    if (pushCount >= 1) drawpointId = pushStack[pushCount - 1];
                }
                if (opcode == JSM_OP_DRAWPOINT) foundDrawpoint = true;

                // Save point.
                if (opcode == JSM_OP_MENUSAVE)   foundMenusave = true;
                if (opcode == JSM_OP_SAVEENABLE)  foundSaveenable = true;
                if (opcode == JSM_OP_PHSENABLE)   foundSaveenable = true;  // also save-point indicator

                // Shop.
                if (opcode == JSM_OP_MENUSHOP) {
                    foundMenushop = true;
                    if (pushCount >= 1) shopId = pushStack[pushCount - 1];
                }

                // Card game.
                if (opcode == JSM_OP_CARDGAME) foundCardgame = true;

                // Ladder.
                if (opcode == JSM_OP_LADDERUP || opcode == JSM_OP_LADDERDOWN ||
                    opcode == JSM_OP_LADDERUP2 || opcode == JSM_OP_LADDERDOWN2)  // FIX-4: add 0x27/0x28
                    foundLadder = true;

                // Map transitions.
                if (opcode == JSM_OP_MAPJUMP || opcode == JSM_OP_MAPJUMP3 ||
                    opcode == JSM_OP_DISCJUMP || opcode == JSM_OP_MAPJUMPO ||
                    opcode == JSM_OP_WORLDMAPJUMP) {
                    foundMapjump = true;
                    methodHasMapjump = true;  // v0.07.84: per-method tracking
                    if (methodMapjumpRel < 0) methodMapjumpRel = ip - (int)scriptStart;
                    // Destination field ID is the deepest (first) push in the sequence.
                    // For MAPJUMP: stack has FieldID, X, Y, TriID (4 pushes).
                    // For MAPJUMP3: stack has FieldID, X, Y, Z, TriID (5 pushes).
                    // The field ID is the oldest push.
                    if (opcode == JSM_OP_MAPJUMP && pushCount >= 4)
                        mapjumpDestField = pushStack[pushCount - 4];
                    else if (opcode == JSM_OP_MAPJUMP3 && pushCount >= 5)
                        mapjumpDestField = pushStack[pushCount - 5];
                    else if (opcode == JSM_OP_DISCJUMP && pushCount >= 5)
                        mapjumpDestField = pushStack[pushCount - 5];
                    // v0.59.0: MAPJUMPO (0x0052 1C30) pops only TWO -- the entrance id
                    // into 0x01CE476C and then the destination field into 0x01CE4762 --
                    // so the destination is the deeper of two, not of four. Measured
                    // across the disc: every one of the 1037 MAPJUMPOs is preceded by a
                    // run of exactly two pushes.
                    else if (opcode == JSM_OP_MAPJUMPO && pushCount >= 2)
                        mapjumpDestField = pushStack[pushCount - 2];
                    else if (opcode == JSM_OP_WORLDMAPJUMP)
                        mapjumpDestField = -2;  // sentinel: goes to world map
                    else if (pushCount >= 1)
                        mapjumpDestField = pushStack[0];  // best guess: oldest push
                    methodMapjumpDest = mapjumpDestField;  // v0.07.84
                }

                // Model assignment / talk.
                if (opcode == JSM_OP_SETMODEL) {
                    foundSetmodel = true;
                    // v0.62.0: keep the inline param -- the chara.one model index.
                    if (info.modelParam < 0 && !bareWord && opcParam >= 0)
                        info.modelParam = (int)opcParam;
                }
                if (opcode == JSM_OP_SETMODEL && m == 0) {
                    foundSetmodelInit = true;  // v0.12.20
                    // v0.17.8.15: chara.one slot capture removed. v0.17.8.11-.14
                    // tried cross-referencing the slot operand against a
                    // parsed chara.one model archive to distinguish NPC from
                    // prop, but the bghall_3 BAT screenshot proved this was
                    // the wrong mechanism entirely (kanban2 IS Xu standing
                    // in the world, regardless of how p048's textures
                    // classify). The catalog now uses the behavior signal
                    // `jsmCategory == 3 && foundSetmodelInit` instead --
                    // exposed via info.hasSetmodelInit below.
                }
                // v0.62.0 (#123): LINEOFF in the init. Exe-confirmed: opcode 0x3A
                // writes 1 and 0x3B writes 0 to the line object's enable byte at
                // +0x194 (handlers 0x0051DCE0 / 0x0051DD00).
                if (opcode == 0x03B && m == 0 && e < 128) s_lineInitOff[e] = true;
                if (opcode == JSM_OP_TALKON)   foundTalkon = true;
                if (opcode == JSM_OP_TALKRADIUS) foundTalkradius = true;  // v0.17.7.1
                // v0.19.8 RE (#5): the ACTUAL interaction opcodes, by exe-confirmed
                // number. The engine interaction check (0x47B460) can only fire when
                // talkRadius(0x1F8)+pushRadius(0x1F6) > 0; TALKRADIUS=0x62 sets the
                // former, PUSHRADIUS=0x63 the latter, and 0x5B is the "is X within my
                // interaction range" test. The legacy JSM_OP_TALKRADIUS constant is
                // 0x056 (a DIFFERENT opcode), so foundTalkradius above never fires.
                if (opcode == 0x62) foundTalkRad62 = true;
                if (opcode == 0x63) foundPushRad63 = true;
                if (opcode == 0x5B) foundProxChk5B = true;

                // Door trigger line.
                if (opcode == JSM_OP_DOORLINEON || opcode == JSM_OP_DOORLINEOFF)
                    foundDoorline = true;

                // v0.12.16: SETLINE interaction zone (opcode 0x29).
                // SETLINE takes 7 params: x1, y1, z1, x2, y2, z2, lineIndex.
                // Extract the line coordinates if they're all literals.
                if (opcode == JSM_OP_SETLINE && pushCount >= 7) {  // FIX-1: real SETLINE 0x39 (was hardcoded 0x29 = MAPJUMP)
                    int slBase = pushCount - 7;
                    bool slAllLit = true;
                    for (int sp = slBase; sp < slBase + 6; sp++) {
                        if ((uint32_t)pushStack[sp] & 0x80000000) { slAllLit = false; break; }
                    }
                    if (slAllLit) {
                        setlineX1 = (int16_t)pushStack[slBase + 0];
                        setlineY1 = (int16_t)pushStack[slBase + 1];
                        setlineZ1 = (int16_t)pushStack[slBase + 2];
                        setlineX2 = (int16_t)pushStack[slBase + 3];
                        setlineY2 = (int16_t)pushStack[slBase + 4];
                        setlineZ2 = (int16_t)pushStack[slBase + 5];
                        foundSetline = true;
                        Log::Field("FieldArchive: [JSMScan] ent%d '%s' SETLINE: "
                                   "(%d,%d,%d)->(%d,%d,%d) center=(%d,%d)",
                                   e, info.symName,
                                   (int)setlineX1, (int)setlineY1, (int)setlineZ1,
                                   (int)setlineX2, (int)setlineY2, (int)setlineZ2,
                                   ((int)setlineX1 + (int)setlineX2) / 2,
                                   ((int)setlineY1 + (int)setlineY2) / 2);
                    }
                }

                // Particle effect (draw points and save points use this).
                if (opcode == JSM_OP_PARTICLEON) foundParticleon = true;

                // Item pickup.  v0.19.x [ADDITEM-DRYRUN]: LOG-ONLY pickup diagnostic
                // (mirrors ShaftCatalogDryRun/[SHAFT-DRYRUN]); it changes NOTHING in the
                // catalog. Fires ONCE per entity per field-load (gated on !foundAdditem,
                // which is per-entity and reset each scan). Purpose: confirm the real
                // ADDITEM operand layout + collected-flag on one BAT before the
                // JSM_ENT_ITEM feature is wired. Cheap; the only fault-prone read
                // (live varblock) is isolated in the SEH helper AdditemVarByte().
                if (opcode == JSM_OP_ADDITEM) {
                    if (!foundAdditem) {
                        char slotsBuf[176]; int sbp = 0; slotsBuf[0] = '\0';
                        for (int k = 0; k < 4 && (pushCount - 1 - k) >= 0; k++) {
                            int32_t v = pushStack[pushCount - 1 - k];
                            sbp += snprintf(slotsBuf + sbp, sizeof(slotsBuf) - sbp, "[-%d]=0x%08X%c ",
                                            k + 1, (unsigned)v, ((uint32_t)v & 0x80000000u) ? 'R' : 'L');
                        }
                        int32_t c1 = (pushCount >= 1) ? pushStack[pushCount - 1] : -1;
                        int32_t c2 = (pushCount >= 2) ? pushStack[pushCount - 2] : -1;
                        bool c1lit = (pushCount >= 1) && (((uint32_t)c1 & 0x80000000u) == 0);
                        bool c2lit = (pushCount >= 2) && (((uint32_t)c2 & 0x80000000u) == 0);
                        const char* n1 = (c1lit && c1 >= 1 && c1 <= 198) ? GetBattleItemName((int)c1) : "(n/a)";
                        const char* n2 = (c2lit && c2 >= 1 && c2 <= 198) ? GetBattleItemName((int)c2) : "(n/a)";
                        if (!n1) n1 = "(null)";
                        if (!n2) n2 = "(null)";
                        Log::Field("FieldArchive: [ADDITEM-DRYRUN] ent%d '%s' m=%d ip=%d pushCount=%d "
                                   "slots: %s| cand id[-1]=%d(%s) id[-2]=%d(%s)",
                                   e, info.symName, m, ip, pushCount, slotsBuf,
                                   (int)c1, n1, (int)c2, n2);
                        Log::Field("FieldArchive: [ADDITEM-DRYRUN] ent%d '%s' hasPosition=%d pos=(%d,%d) "
                                   "talkSetup(TALKON|TALKRADIUS)=%d setline=%d",
                                   e, info.symName, info.hasPosition ? 1 : 0, (int)info.posX, (int)info.posY,
                                   (foundTalkon || foundTalkradius) ? 1 : 0, foundSetline ? 1 : 0);
                        int  bsVar = -1, bsCmp = 0; bool bsJpf = false, bsHaveLit = false, bsHavePshm = false;
                        int  lo = ip - 24; if (lo < (int)scriptStart) lo = (int)scriptStart;
                        for (int k = ip - 1; k >= lo; k--) {
                            uint32_t w = scriptData[k]; uint8_t hb = (uint8_t)(w >> 24);
                            if (hb == 0x02) { bsJpf = true; }
                            else if (hb == 0x00) { if (bsJpf && !bsHaveLit) { bsCmp = (int)(int32_t)w; bsHaveLit = true; } }
                            else if (hb == 0x07 || hb == 0x09 || hb == 0x0A) {
                                int32_t pp = (int32_t)(w & 0x00FFFFFF); if (w & 0x00800000) pp |= (int32_t)0xFF000000;
                                if (bsJpf && !bsHavePshm && pp >= 0 && pp < 0x2000) { bsVar = pp; bsHavePshm = true; }
                            }
                        }
                        int liveTrk = (guardSeen && guardAddr >= 0 && guardAddr < 0x2000)
                                    ? (int)AdditemVarByte((unsigned)(uint16_t)guardAddr) : -1;
                        int liveBsc = (bsVar >= 0) ? (int)AdditemVarByte((unsigned)bsVar) : -1;
                        Log::Field("FieldArchive: [ADDITEM-DRYRUN] ent%d '%s' guard.tracker[seen=%d addr=0x%04X val=%d jpf=%d live=%d]"
                                   " guard.backscan[found=%d var=0x%04X cmp=%d jpf=%d live=%d]",
                                   e, info.symName, guardSeen ? 1 : 0, (unsigned)(uint16_t)guardAddr, (int)guardVal,
                                   guardHadJpf ? 1 : 0, liveTrk,
                                   bsHavePshm ? 1 : 0, (unsigned)(bsVar < 0 ? 0 : bsVar), bsHaveLit ? bsCmp : 0, bsJpf ? 1 : 0, liveBsc);
                    }
                    foundAdditem = true;
                }

                // v0.07.82: Camera/scroll opcodes for Line entity classification.
                if (opcode == JSM_OP_BGDRAW || opcode == JSM_OP_BGOFF ||
                    opcode == JSM_OP_BGANIME || opcode == JSM_OP_BGANIMESPEED)
                    foundBgdraw = true;
                if (opcode == JSM_OP_DSCROLL || opcode == JSM_OP_LSCROLL ||
                    opcode == JSM_OP_CSCROLL || opcode == JSM_OP_DSCROLLA ||
                    opcode == JSM_OP_LSCROLLA || opcode == JSM_OP_CSCROLLA ||
                    opcode == JSM_OP_SCROLLSYNC ||
                    opcode == JSM_OP_DSCROLLP || opcode == JSM_OP_LSCROLLP ||
                    opcode == JSM_OP_CSCROLLP || opcode == JSM_OP_SETCAMERA)
                    foundScroll = true;
                if (opcode == JSM_OP_SHOW || opcode == JSM_OP_HIDE ||
                    opcode == JSM_OP_USE || opcode == JSM_OP_UNUSE ||
                    opcode == JSM_OP_MES || opcode == JSM_OP_ASK ||
                    opcode == JSM_OP_AMES || opcode == JSM_OP_AASK ||
                    opcode == JSM_OP_MOVE ||
                    opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW)
                    foundEventOp = true;
                if (opcode == JSM_OP_HIDE) foundHide = true;  // v0.19.4 diag: self-hide signal for pickup detection
                if (opcode == JSM_OP_HIDE   && m == 0) hideInInit   = true;   // v0.75.0
                if (opcode == JSM_OP_TALKON && m == 0) talkOnInInit = true;   // v0.75.0
                if (opcode == JSM_OP_BATTLE) foundBattle = true;
                // v0.120.0 (#centra): BTNTEST consumes ONE argument, so the mask
                // is the top of the current push run by construction -- the same
                // property every other argument read here relies on. A Centra
                // ladder records 192, Cross|Square.
                if (opcode == JSM_OP_BTNTEST && m >= 1 && pushCount >= 1 &&
                    touchBtnMask == 0) {
                    const int32_t maskV = pushStack[pushCount - 1];
                    if (maskV > 0 && maskV <= 0xFFFF) touchBtnMask = (uint16_t)maskV;
                }

                // v0.07.98: Track dialog opcodes specifically (MES/ASK/AMES/AASK).
                // foundEventOp is too broad (includes SHOW/HIDE/MOVE/REQ) and would
                // false-positive on lighting/animation controllers.
                if (opcode == JSM_OP_MES || opcode == JSM_OP_ASK ||
                    opcode == JSM_OP_AMES || opcode == JSM_OP_AASK)
                    foundDialogOp = true;

                // v0.07.84: Extract REQ/REQSW/REQEW call targets for indirect MAPJUMP detection.
                // v0.12.20: Also count REQ opcodes per entity (stack-independent).
                if ((opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) && e < 128)
                    s_reqOpcodeCount[e]++;
                // v0.19.7 (#5): mark the REQ TARGET as a real dispatch target. The
                // target entity is the opcode's INLINE param (opcParam), NOT a stack
                // value -- exe RE of the REQ handler (0x51CD60) showed arg2 indexes
                // entityPtrTable directly; the popped stack slots are method+priority,
                // which is why the old stack-based s_entityReqs read reqResolved=0.
                // BAT-confirmed on bghall_1: 'elelight' REQs resolve to seito3/seito4.
                // The director junk-gate keeps a promoted Object only if it is a REQ
                // target; one that nothing REQs has no interaction path and is dropped.
                // v0.58.0: opcParam is a RUNTIME SLOT (the REQ handler at
                // 0x0051CD8D indexes the 0x01D9D020 object table with it), and
                // that table is ordered Others, Lines, Backgrounds, Doors -- so
                // it is not a group index. s_isReqTarget[] is read back by group,
                // so translate. Before this, on any field with a non-Other entity
                // the whole set was off by nLines+nBackgrounds and the director
                // gate kept and dropped essentially arbitrary entities.
                if ((opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) &&
                    opcParam >= 0 && opcParam < JSM_ORDER_MAX) {
                    int tgtGroup = order.groupOfSlot[opcParam];
                    if (tgtGroup >= 0 && tgtGroup < 128) s_isReqTarget[tgtGroup] = true;
                }
                // v0.19.6 [REQ-TARGET] diagnostic (director-gate): exe RE of the REQ handlers
                // (0x14/0x15/0x16 @ 0x51CD60/CED0/D060) shows the TARGET entity is the opcode's
                // INLINE PARAM (arg2 -> entityPtrTable[param]), NOT a stack value -- which is
                // exactly why reqResolved=0 (the scan reads the simulated stack, which holds
                // PSHM markers). Log the inline-param target + its SYM so a bghall BAT confirms
                // whether elelight's REQs point at the real interactive entities (seito*/aniki/
                // directory). If so, the director gate reads targets STATICALLY from opcParam
                // and no runtime VM hook is needed. Log-only; zero classification change.
                if (opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) {
                    int tgtIdx = (opcParam >= 0 && opcParam < JSM_ORDER_MAX)
                                 ? order.groupOfSlot[opcParam] : -1;
                    const char* tgtName = "?";
                    if (tgtIdx >= 0 && order.symIdx[tgtIdx] >= 0 &&
                        order.symIdx[tgtIdx] < symCount)
                        tgtName = symNames[order.symIdx[tgtIdx]];
                    int st1 = (pushCount >= 1) ? pushStack[pushCount - 1] : -999;
                    int st2 = (pushCount >= 2) ? pushStack[pushCount - 2] : -999;
                    Log::Field("FieldArchive: [REQ-TARGET] ent%d '%s' m=%d opcParam=%d -> "
                               "target ent%d '%s' | stackTop=%d stack2=%d pushCount=%d",
                               e, info.symName, m, opcParam, tgtIdx, tgtName, st1, st2, pushCount);
                }
                // v0.62.0 (#123): REQ, recorded from what the opcode actually takes.
                //
                // The old read wanted THREE stack values (entity, method,
                // priority) and took the target entity off the stack. REQ takes
                // two: the compiler emits `PSHN_L priority; PSHN_L methodId;
                // REQ <entity>` and the TARGET IS THE INLINE PARAM -- the handler
                // at 0x0051CD8D indexes the object table at 0x01D9D020 with it.
                // Since v0.59.0 clears the push run after every non-push opcode,
                // pushCount at a REQ is exactly 2, so `pushCount >= 3` was never
                // true and s_entityReqs has been EMPTY on every field since. The
                // scanner's own [REQ-TARGET] diagnostic has been printing the
                // right answer beside it the whole time ("stackTop=48 stack2=2").
                //
                // The method id is the top push, and it indexes the entry-point
                // table exactly as `methodIdx` does below -- the same number the
                // method's own prologue (0x05) carries. That is what makes
                // s_methodMapjumps[targetMethod] a legal lookup.
                //
                // The inline param is a RUNTIME SLOT, so it goes through
                // order.groupOfSlot the same way s_isReqTarget does.
                if ((opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) &&
                    e < 128) {
                    int reqTargetEnt  = (opcParam >= 0 && opcParam < JSM_ORDER_MAX)
                                        ? order.groupOfSlot[opcParam] : -1;
                    int reqTargetMeth = (pushCount >= 1) ? pushStack[pushCount - 1] : -1;
                    if (reqTargetEnt >= 0 && reqTargetEnt < totalEntities &&
                        reqTargetMeth >= 0 && reqTargetMeth < MAX_METHOD_MAPJUMPS &&
                        s_entityReqs[e].count < MAX_REQ_PER_ENTITY) {
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetEntity = reqTargetEnt;
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetMethod = reqTargetMeth;
                        s_entityReqs[e].calls[s_entityReqs[e].count].srcMethod    = methodIdx;
                        s_entityReqs[e].calls[s_entityReqs[e].count].srcRel       = ip - (int)scriptStart;
                        s_entityReqs[e].count++;
                    }
                    if (pushCount >= 2) pushCount -= 2;
                }

                // v0.59.0: end of a non-push opcode -- the argument run it consumed is
                // finished. Clearing here is what makes pushStack[pushCount - N] mean
                // "the N arguments immediately before this opcode", which is exactly how
                // the compiler emits them. The old code carried a simulated stack across
                // the whole method and modelled pops opcode by opcode, so the depth
                // drifted and an extraction could read a value belonging to some earlier
                // statement. Every consumer above has already read what it needed.
                pushCount = 0;
            }
            // v0.62.3 (#123): if THIS method carries the MAPJUMP, its own leading
            // guard is the gate on the exit. sspod2's `pod` line is the case:
            // its touch script opens `var[256] == 2556` and ends in MAPJUMPO 638,
            // so before the story reaches 2556 the line is inert -- and the
            // catalog was both offering it as "Exit to Desert 1" and letting the
            // screen filter treat it as a wall, which is what hid Ellone.
            // v0.132.0 (#shumi): walk the method for guards instead of decoding
            // only the one at its first word. tmkobo2's `Munbamini` chains three
            // -- var[607]==32, var[607]==64, then var[623]==16 skipping 79 words
            // -- and the MAPJUMPO to the elevator sits inside the third. Under
            // the leading-guard-only test it read as an ungated exit, so the
            // catalog offered "Exit to Shumi Village - Elevator" in a room where
            // nothing happens when you walk onto it. See gated_exit_model.inl.
            if (methodHasMapjump && !info.hasGate &&
                scriptStart < scriptDataDwords && methodMapjumpRel >= 0) {
                const int avail = (int)(scriptDataDwords - scriptStart);
                const int lim   = GatedExitScanLimit(methodMapjumpRel, avail);
                // Link 0 is the method's leading guard, which carries the LBL.
                JsmGate g = JsmDecodeGate(&scriptData[scriptStart], avail);
                int at = 0;
                for (int hop = 0; hop < GATED_EXIT_MAX_CHAIN && g.ok; hop++) {
                    if (GatedExitJumpIsInsideGuard(at, g.skipTo, methodMapjumpRel)) {
                        info.hasGate   = true;
                        info.gateAddr  = g.addr;
                        info.gateWidth = g.width;
                        info.gateOp    = g.op;
                        info.gateValue = g.value;
                        break;
                    }
                    const int next = at + g.skipTo;   // where this link's else lands
                    if (next <= at || next >= lim) break;
                    at = next;
                    g  = JsmDecodeGateAt(&scriptData[scriptStart + at], avail - at);
                }
            }
            // v0.07.84: Record per-method MAPJUMP for REQ-following.
            if (methodHasMapjump && methodIdx >= 0 && methodIdx < MAX_METHOD_MAPJUMPS) {
                s_methodMapjumps[methodIdx].found = true;
                s_methodMapjumps[methodIdx].destFieldId = methodMapjumpDest;
                // v0.07.87: Copy PSHM_W addresses for variable-dispatch matching.
                int copyCount = (methodPshmCount < MAX_PSHM_PER_METHOD) ? methodPshmCount : MAX_PSHM_PER_METHOD;
                for (int p = 0; p < copyCount; p++)
                    s_methodMapjumps[methodIdx].pshmAddrs[p] = methodPshmAddrs[p];
                s_methodMapjumps[methodIdx].pshmCount = copyCount;
            }
        }

        // Per-entity classification + post-passes. Extracted to
        // field_archive_jsm_classify.inl (v0.18.3.294) to get this file back
        // under the CI size ceiling (GitHub #37). The fragment runs inline here,
        // operates on this loop's locals. It is brace-balanced and does NOT
        // close the for-loop -- `outCount++;` and the loop brace stay below.
        // Pure textual move -- no logic change.
        #include "field_archive_jsm_classify.inl"

        // v0.75.0 (#112): hidden AND talk-enabled by its own init is an invisible
        // interaction point, not an absent object. See JSMEntityInfo for why the
        // catalog's HIDE filter has to know the difference.
        info.invisibleTalkTarget = (hideInInit && talkOnInInit);

        // v0.19.4 [MODELSIG] (#pickup-vs-silent-npc): one compact ground-truth line per
        // model-bearing "Other" entity -- the full interaction-signal set + final producer
        // type. Lets v0.19.5 design the pickup-vs-real-NPC-vs-silent-NPC discriminator from
        // data, not a guess. Log-only; changes no classification.
        if (info.jsmCategory == 3 && foundSetmodel) {
            Log::Field("FieldArchive: [MODELSIG] ent%d '%s' type=%s pos=%d(%d,%d) "
                       "talkon=%d talkrad056=%d dialog=%d extdisp=%d additem=%d hide=%d "
                       "setline=%d reqcount=%d setmodelInit=%d nonInitWr=%d additemLit=%d pickup=%d "
                       "talkrad62=%d pushrad63=%d prox5B=%d invisTalk=%d",
                       e, info.symName, JSMEntityTypeName(info.type),
                       info.hasPosition ? 1 : 0, (int)info.posX, (int)info.posY,
                       foundTalkon ? 1 : 0, foundTalkradius ? 1 : 0,
                       foundDialogOp ? 1 : 0, foundExtDispatch ? 1 : 0,
                       foundAdditem ? 1 : 0, foundHide ? 1 : 0,
                       foundSetline ? 1 : 0,
                       (e < 128 ? s_reqOpcodeCount[e] : -1),
                       foundSetmodelInit ? 1 : 0,
                       foundNonInitVarWrite ? 1 : 0, sawLitAdditem ? 1 : 0,
                       (foundSetmodel && !foundDialogOp && (foundNonInitVarWrite || sawLitAdditem)) ? 1 : 0,
                       foundTalkRad62 ? 1 : 0, foundPushRad63 ? 1 : 0, foundProxChk5B ? 1 : 0,
                       info.invisibleTalkTarget ? 1 : 0);
        }

        outCount++;
    }

#include "field_archive_jsm_reqpass.inl"
    // v0.16.3: Director-dispatched interaction detection (extracted to director.inl).
    // See field_archive_jsm_director.inl for the full implementation. This call
    // replaces the inline DIAGNOSTIC + Director-detection-post-pass blocks that
    // used to live here.
    RunDirectorDetection(fieldName, outEntities, outCount,
                         countDoors, countLines, countBg,
                         symNames, symCount);

    // v0.17.7.5: Static destField resolver pass.
    //
    // v0.17.7.4 BAT confirmed the forward scanner above mis-identifies the
    // destField source for SCREEN_BOUND lines on bghall_3/5 -- the reported
    // PSHM addresses (0x0002, 0x023A, 0x01F6) hold values (14381, 0, 0) at
    // MAPJUMP3 fire time that don't match the engine's chosen destination
    // (field 170 then 165). Root cause: pushCount accumulates across basic
    // block boundaries and the 8-deep pushStack truncates older entries.
    //
    // The resolver in mapjump_resolver.inl re-walks each SCREEN_BOUND line's
    // bytecode method-by-method with proper basic-block awareness and a
    // wider 32-slot abstract stack. When it finds a MAPJUMP/MAPJUMP3, it
    // identifies the actual destField source (literal or PSHM_W ref) and
    // overwrites info.param. The existing [PSHM-DEST] resolution in
    // HookedFieldScriptsInit then resolves PSHM markers via the live
    // varblock at field load -- which is the same downstream path the
    // forward scanner's (incorrect) markers already flowed through, just
    // now with the RIGHT marker.
    //
    // Build parallel arrays describing the EntityGroup layout so the
    // resolver doesn't need access to the function-local EntityGroup type.
    {
        static int methodStartIdxs[128];
        static int methodCounts[128];
        int safeCount = (totalEntities < 128) ? totalEntities : 128;
        for (int e = 0; e < safeCount; e++) {
            methodStartIdxs[e] = groups[e].startMethodIdx;
            methodCounts[e]    = groups[e].methodCount;
        }
        MapjumpResolver::Run(fieldName,
                             scriptData, scriptDataDwords,
                             entryPoints, totalMethods,
                             methodStartIdxs, methodCounts, safeCount,
                             outEntities, outCount);
    }

    // v0.17.8.9 [size]: four long-disabled `if (false)` diagnostic blocks were
    // removed here to keep this file under the size ceiling -- POPM_W-address
    // dump (v0.07.88), PSHM_W-coords summary (v0.07.99), per-method MAPJUMP dump
    // (v0.07.89), and the INF-gateway dump (v0.07.93). All were dead code
    // (guarded by `if (false)`) and are recoverable from git history if needed.

#include "field_archive_jsm_linepass.inl"
    // v0.17.8.8: Save-point wiring diagnostic. For every entity classified as a
    // Save Point, report whether it resolved a navigable position (so it can be
    // injected as a standalone "Save Point") and whether any Line was flagged as
    // its trigger (signal a/b above). When a field's save point has neither -- as
    // on bghall_1, whose 'savePoint' has PSHM-only X/Y -- this line tells us the
    // save must be wired some other way (e.g. proximity on the sparkle with no
    // line link), which is the data needed to extend detection. One line per save
    // point; save points are rare (0-1 per field), so this is not log spam.
    {
        int saveLineCount = 0;
        for (int i = 0; i < outCount; i++)
            if (outEntities[i].isSaveLine) saveLineCount++;
        for (int i = 0; i < outCount; i++) {
            if (outEntities[i].type != JSM_ENT_SAVE_POINT) continue;
            Log::Field("FieldArchive: [JSMScan] save-wiring: ent%d '%s' hasPosition=%d "
                       "hasPshmCoords=%d -- field has %d save-line(s) flagged [v0.17.8.8]",
                       outEntities[i].jsmIndex, outEntities[i].symName,
                       outEntities[i].hasPosition ? 1 : 0,
                       outEntities[i].hasPshmCoords ? 1 : 0, saveLineCount);
        }
    }

    // --- Log results ---
    int drawPoints = 0, savePoints = 0, shops = 0, cards = 0, ladders = 0, exits = 0;
    int lineCameraPans = 0, lineScreenBounds = 0, lineEvents = 0, interactiveObjects = 0;
    int lineInteractive = 0;  // v0.12.24
    int directors = 0;  // v0.12.20
    int pshmCoords = 0;  // v0.07.99: entities with PSHM_W coordinate markers
    for (int i = 0; i < outCount; i++) {
        const JSMEntityInfo& e = outEntities[i];
        switch (e.type) {
            case JSM_ENT_DRAW_POINT:        drawPoints++; break;
            case JSM_ENT_SAVE_POINT:        savePoints++; break;
            case JSM_ENT_SHOP:              shops++; break;
            case JSM_ENT_CARD_GAME:         cards++; break;
            case JSM_ENT_LADDER:            ladders++; break;
            case JSM_ENT_MAP_EXIT:          exits++; break;
            case JSM_ENT_LINE_CAMERA_PAN:   lineCameraPans++; break;
            case JSM_ENT_LINE_SCREEN_BOUND: lineScreenBounds++; break;
            case JSM_ENT_LINE_EVENT:        lineEvents++; break;
            case JSM_ENT_LINE_INTERACTIVE:  lineInteractive++; break;
            case JSM_ENT_INTERACTIVE_OBJECT: interactiveObjects++; break;
            case JSM_ENT_DIRECTOR:          directors++; break;
            default: break;
        }
        if (outEntities[i].hasPshmCoords) pshmCoords++;
    }
    Log::Field("FieldArchive: [JSMScan] '%s' results: %d entities scanned — "
               "DrawPts=%d SavePts=%d Shops=%d Cards=%d Ladders=%d Exits=%d "
               "LineCamPan=%d LineScreenBd=%d LineEvent=%d LineInteract=%d IntObj=%d Dir=%d PshmCoord=%d",
               fieldName, outCount, drawPoints, savePoints, shops, cards, ladders, exits,
               lineCameraPans, lineScreenBounds, lineEvents, lineInteractive, interactiveObjects, directors, pshmCoords);

    // Detailed log for each classified entity.
    for (int i = 0; i < outCount; i++) {
        const JSMEntityInfo& e = outEntities[i];
        if (e.type == JSM_ENT_UNKNOWN || e.type == JSM_ENT_BACKGROUND ||
            e.type == JSM_ENT_DOOR || e.type == JSM_ENT_LINE_TRIGGER ||
            e.type == JSM_ENT_LINE_CAMERA_PAN)  // v0.07.82: camera pans are too numerous to log individually
            continue;  // skip uninteresting entries to reduce log noise

        const char* destName = "";
        if (e.type == JSM_ENT_MAP_EXIT && e.param >= 0) {
            destName = GetFieldNameById((uint16_t)e.param);
            if (!destName) destName = "(unknown)";
        }

        Log::Field("FieldArchive: [JSMScan]   ent%d cat=%d type=%s sym='%s' "
                   "pos=%s(%d,%d,%d tri=%d) param=%d%s%s%s",
                   e.jsmIndex, e.jsmCategory, JSMEntityTypeName(e.type),
                   e.symName,
                   e.hasPosition ? "YES" : (e.hasPshmCoords ? "PSHM" : "no"),
                   (int)e.posX, (int)e.posY, (int)e.posZ, (int)e.posTriangle,
                   e.param,
                   (e.type == JSM_ENT_MAP_EXIT && e.param >= 0) ? " dest=" : "",
                   destName,
                   e.hasPshmCoords ? " [PSHM_W]" : "");
    }

    // v0.17.8.5.x [dorm-diag] DIAGNOSTIC removed in v0.17.8.6. The bgryo2_1 dorm
    // bed was identified as ent0 'squall' (a Line whose "rest?" AASK is reached via a
    // runtime-supplied 0x1C dispatch); it is now classified LINE_INTERACTIVE by the
    // extDisp rule in the Line-classification block above. See DEVNOTES.

    // v0.17.8.9 [save-point]: the LOCAL bghall_1 script-dump diagnostic was removed
    // once it confirmed the save signal -- 'selphie' literally pushes SAVEENABLE
    // (0x12F) + PHSENABLE (0x130), now detected by the own-script-constant path in
    // the Line-classification block (signal-a). See DEVNOTES / field_archive_jsm_dump.inl.

    // v0.19.x [ITEMGATE-VARS] (#item-pickups): compact var-bank snapshot once per
    // field-load. Diff two dorm loads (magazine uncollected vs collected) to find the
    // "collected" flag byte. (The verbose [ITEMDUMP] opcode dump is retired -- it
    // already gave us 'hon's opcodes; DumpItemPickupScripts is left defined but uncalled.)
    //
    // v0.19.5: item-pickup investigation dumps RETIRED. The verbose [ITEMDUMP]
    // per-entity opcode dump did its job -- it revealed both pickup mechanisms (dorm
    // Weapons Monthly = 'saveline0' var 450 + ADDITEM 0x125; hotel Timber Maniacs =
    // 'Urakata' var 304, no ADDITEM), from which v0.19.5's isItemPickup discriminator
    // was built and BAT-confirmed ("Item 1" in-game). The call is disabled to keep
    // normal field-load logs clean; the compact [MODELSIG] line above (with the
    // pickup= field) stays for ongoing item work. DumpItemPickupScripts / DumpItemGateVars
    // remain DEFINED (dump.inl / above) -- re-enable a call if a future pickup mechanism
    // needs the full opcode or var-bank stream.
    // DumpItemPickupScripts(fieldName);

    return true;
}
