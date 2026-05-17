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

    // Diagnostic: log entity group boundaries with SYM names.
    for (int e = 0; e < totalEntities; e++) {
        // SYM excludes doors. Current assumption: entities are ordered
        // Door[0..D-1], Line[D..D+L-1], Bg[D+L..D+L+B-1], Other[D+L+B..total-1]
        // so symIdx = e - countDoors.
        int symIdx = e - countDoors;
        const char* sym = (symIdx >= 0 && symIdx < symCount) ? symNames[symIdx] : "(door)";
        if (e < 15 || groups[e].methodCount > 0) {
            Log::Field("FieldArchive: [JSMScan] grp[%d] raw=0x%04X methods=%d startIdx=%d sym='%s'",
                       e, (unsigned)groups[e].rawEntry, groups[e].methodCount,
                       groups[e].startMethodIdx, sym);
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

    // --- Scan each entity ---
    for (int e = 0; e < totalEntities && outCount < maxEntities; e++) {
        JSMEntityInfo& info = outEntities[outCount];
        memset(&info, 0, sizeof(info));
        info.jsmIndex = e;
        info.param = -1;

        // Determine JSM category from index ranges.
        // Order: Door[0..D-1], Line[D..D+L-1], Bg[D+L..D+L+B-1], Other[D+L+B..total-1]
        int catStart = 0;
        if (e < countDoors) {
            info.jsmCategory = 0;  // Door
            info.type = JSM_ENT_DOOR;
        } else if (e < countDoors + countLines) {
            info.jsmCategory = 1;  // Line
            info.type = JSM_ENT_LINE_TRIGGER;
        } else if (e < countDoors + countLines + countBg) {
            info.jsmCategory = 2;  // Background
            info.type = JSM_ENT_BACKGROUND;
        } else {
            info.jsmCategory = 3;  // Other
            info.type = JSM_ENT_UNKNOWN;  // will be classified by opcodes
        }

        // Map to SYM name. SYM excludes doors: SYM[i] = JSM entity[i + countDoors].
        int symIdx = e - countDoors;
        if (symIdx >= 0 && symIdx < symCount) {
            strncpy(info.symName, symNames[symIdx], 31);
            info.symName[31] = '\0';
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
        bool foundMenushop     = false;
        bool foundCardgame     = false;
        bool foundLadder       = false;
        bool foundMapjump      = false;
        bool foundSetmodel     = false;
        bool foundSetmodelInit = false;  // v0.12.20: SETMODEL specifically in method 0 (init)
        bool foundTalkon       = false;
        bool foundDoorline     = false;
        bool foundParticleon   = false;
        bool foundAdditem      = false;
        int  mapjumpDestField  = -1;
        int  drawpointId       = -1;
        int  shopId            = -1;

        // v0.07.82: Camera/scroll/event flags for Line entity classification.
        bool foundBgdraw       = false;  // BGDRAW or BGOFF
        bool foundScroll       = false;  // any DSCROLL/LSCROLL/CSCROLL/SETCAMERA
        bool foundEventOp      = false;  // SHOW/HIDE/USE/UNUSE/MES/ASK/BATTLE/MOVE/REQ
        bool foundDialogOp     = false;  // v0.07.98: MES/ASK/AMES/AASK specifically (for interactive object detection)
        bool foundExtDispatch  = false;  // v0.07.98: 0x1C fired with PSHM_W value (runtime-dispatched extended opcode)
        bool foundBattle       = false;  // BATTLE specifically
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

            // v0.07.87: Per-method PSHM_W address tracking for variable-dispatch.
            int32_t methodPshmAddrs[MAX_PSHM_PER_METHOD] = {};
            int methodPshmCount = 0;

            for (int ip = (int)scriptStart; ip < (int)scriptEnd && ip < scriptDataDwords; ip++) {
                uint32_t word = scriptData[ip];  // native LE read of raw file bytes
                uint8_t highByte = (uint8_t)(word >> 24);

                // v0.07.75: SVDUMP diagnostic logging disabled — position extraction confirmed.
                bool detailDump = false;

                if (highByte == 0) {
                    // Push literal: value = full dword (high byte is 0, so max 0x00FFFFFF)
                    int32_t pushVal = (int32_t)word;
                    if (detailDump) {
                        Log::Field("FieldArchive: [SVDUMP] ent=%d m=%d ip=%d PUSH 0x%X (%d) stk=%d",
                                   e, m, ip, (unsigned)pushVal, (int)pushVal, pushCount + 1);
                    }
                    if (pushCount < PUSH_STACK_MAX) {
                        pushStack[pushCount++] = pushVal;
                    } else {
                        for (int s = 0; s < PUSH_STACK_MAX - 1; s++)
                            pushStack[s] = pushStack[s + 1];
                        pushStack[PUSH_STACK_MAX - 1] = pushVal;
                    }
                    continue;
                }

                // Opcode: high byte = primary opcode index (0x01-0xFF).
                // Low 24 bits = param (sign-extended if bit 23 set).
                uint16_t opcode = (uint16_t)highByte;
                int32_t opcParam = (int32_t)(word & 0x00FFFFFF);
                if (word & 0x00800000) opcParam |= (int32_t)0xFF000000;  // sign extend

                if (detailDump) {
                    Log::Field("FieldArchive: [SVDUMP] ent=%d m=%d ip=%d OP 0x%02X param=%d stk=%d word=0x%08X",
                               e, m, ip, (int)opcode, (int)opcParam, pushCount, word);
                }

                // Extended opcodes: primary 0x1C is a prefix for extended dispatch.
                // The engine's 0x1C handler POPS the extended opcode index from the
                // script VM stack (pushed by a preceding PSHN_L), then calls table[popped].
                if (opcode == 0x1C && pushCount > 0) {
                    // Extended dispatch: the 0x1C handler POPS the extended opcode
                    // index from the stack (NOT from the instruction param).
                    // The preceding PSHN_L pushed the dispatch table index.
                    int32_t extOp = pushStack[--pushCount];  // pop
                    // v0.07.75: 0x1C expansion logging disabled — opcode dispatch confirmed.
                    // (Was limited to 100 per field for diagnostic purposes.)
                    if (extOp >= 0 && extOp < 0x200) {
                        opcode = (uint16_t)extOp;
                    } else if (((uint32_t)extOp & 0xFFFF0000u) == 0x80000000u) {
                        // v0.07.98/v0.08.00/v0.08.13: Value came from PSHM_W (runtime memory push).
                        // Tightened: only 0x8000xxxx pattern, not negative passthrough literals.
                        // We can't resolve the actual opcode statically, but this
                        // entity uses runtime-dispatched extended opcodes — which
                        // often include MES/ASK for interactive objects.
                        foundExtDispatch = true;
                    }
                } else if (opcode == 0x1C && pushCount == 0) {
                    // Stack empty when 0x1C fires — our simulation lost track.
                    // The dispatch index was pushed but consumed by an unmodeled opcode.
                    // v0.07.98: Still counts as extended dispatch usage for interactive
                    // object detection — entities using 0x1C with lost stack likely call
                    // MES/ASK via runtime variable dispatch.
                    foundExtDispatch = true;
                    static int s_emptyCount = 0;
                    if (s_emptyCount < 5) {
                        Log::Field("FieldArchive: [JSMScan] 0x1C EMPTY STACK: ent=%d method=%d", e, m);
                        s_emptyCount++;
                    }
                }

                // Model stack effects of known primary opcodes instead of
                // flushing the entire stack. The old flush-all approach caused
                // 0x1C to always hit EMPTY STACK for save/draw point entities
                // whose dispatch index comes from PSHM_W (runtime memory push).
                //
                // For opcodes where we know the stack effect, model it.
                // For unknown opcodes, leave the stack untouched.
                // This is less precise but FAR better than flushing everything.
                if (highByte != 0 && opcode != 0x1C) {
                    switch (highByte) {
                        // Push opcodes: push 1 value from game memory onto VM stack.
                        // We push the param (memory address) as a placeholder.
                        case 0x07: // PSHM_W - push word from memory
                        case 0x09: // PSHM_B - push byte from memory
                        case 0x0A: // PSHM_L - push long from memory
                        case 0x0C: // PSHSM_W - push from special memory
                        case 0x0D: // PSHSM_B - push byte from special memory
                        {
                            // Push a marker: bit 31 flags it as "from memory".
                            // v0.08.00: Changed from 0x00FF0000 to bit 31. The old
                            // 0x00FF pattern collided with negative literal values
                            // (e.g. push of -1484 = 0x00FFFA34 matched the marker).
                            // Literal pushes max at 0x00FFFFFF, never setting bit 31.
                            //
                            // v0.08.13: PSHM_W negative-param passthrough.
                            // Deep research confirmed: negative PSHM_W params cannot be
                            // valid varblock offsets (unsigned). The engine returns them
                            // as literal coordinate values. Each axis is resolved
                            // independently, so one SET3 can mix varblock + passthrough.
                            // Treat negative params as literals (like PSHN_L push).
                            int32_t marker;
                            if (highByte == 0x07 && opcParam < 0) {
                                // Passthrough: negative param IS the coordinate value.
                                // Push as literal (no bit31 marker) so SET3 extraction
                                // treats it as a resolved coordinate.
                                marker = opcParam;
                            } else {
                                marker = (int32_t)(0x80000000u | (uint32_t)(opcParam & 0xFFFF));  // bit31 + mem addr
                            }
                            if (pushCount < PUSH_STACK_MAX)
                                pushStack[pushCount++] = marker;
                            // v0.07.87: Record PSHM_W reads for variable-dispatch detection.
                            if (highByte == 0x07 && methodPshmCount < MAX_PSHM_PER_METHOD) {
                                // Deduplicate: only add if not already tracked.
                                bool dup = false;
                                for (int d = 0; d < methodPshmCount; d++)
                                    if (methodPshmAddrs[d] == opcParam) { dup = true; break; }
                                if (!dup)
                                    methodPshmAddrs[methodPshmCount++] = opcParam;
                            }
                            break;
                        }
                        // Pop 1 opcodes:
                        case 0x02: // JPF - conditional jump, pops condition
                        case 0x08: // POPM_W - pop to memory word
                        case 0x0B: // POPM_L - pop to memory long
                            // v0.12.20: Record PUSH+POPM_W pairs in init method for Director variable maps.
                            // Must capture BEFORE the pop. Only record literal values (no PSHM markers).
                            if ((highByte == 0x08 || highByte == 0x0B) && m == 0 && e < 128 &&
                                pushCount > 0 && ((uint32_t)pushStack[pushCount-1] & 0x80000000u) == 0 &&
                                s_initVarMaps[e].count < 64) {
                                s_initVarMaps[e].writes[s_initVarMaps[e].count].addr = opcParam;
                                s_initVarMaps[e].writes[s_initVarMaps[e].count].value = pushStack[pushCount-1];
                                s_initVarMaps[e].count++;
                            }
                            if (pushCount > 0) pushCount--;
                            // v0.07.87: Record POPM_W writes for variable-dispatch detection.
                            if ((highByte == 0x08 || highByte == 0x0B) && e < 128) {
                                if (s_entityPopms[e].count < MAX_POPM_PER_ENTITY) {
                                    bool dup = false;
                                    for (int d = 0; d < s_entityPopms[e].count; d++)
                                        if (s_entityPopms[e].addrs[d] == opcParam) { dup = true; break; }
                                    if (!dup)
                                        s_entityPopms[e].addrs[s_entityPopms[e].count++] = opcParam;
                                }
                            }
                            break;
                        // No stack effect (control flow only):
                        case 0x01: // JMP
                        case 0x03: // JMPB
                        case 0x04: // JMPF variant
                        case 0x05: // LBL
                        case 0x06: // RET
                            break;
                        // All other primary opcodes: unknown stack effect.
                        // Don't flush — leave stack as-is. Some opcodes pop args
                        // and push results, but we can't model them all. Leaving
                        // the stack alone gives 0x1C the best chance of finding
                        // its dispatch index.
                        default:
                            break;
                    }
                }

                // --- Check for signature opcodes ---

                // SET3: position from init script (method 0).
                // Primary: 4 stack params (X, Y, Z, triangleId) — works for literal pushes.
                // Fallback: 3 stack params (X, Y, Z) + triangle from opcParam.
                // The fallback handles entities that use PSHM_W for coordinates
                // (e.g. bggate_2 dp01: X/Y/Z from PSHM_W markers, tri=194 in opcParam).
                // v0.07.75: Added 3-param fallback for draw point position extraction.
                // v0.07.99: PSHM_W marker detection — when coordinates come from runtime
                // memory, store the memory addresses instead of garbage position values.
                if (opcode == JSM_OP_SET3 && m == 0 && !info.hasPosition && !info.hasPshmCoords) {
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
                            uint16_t tri = (paramCount == 4) ? (uint16_t)pushStack[coordBase + 3] : (uint16_t)opcParam;
                            Log::Field("FieldArchive: [SET3-DIAG] ent%d '%s' PSHM_W coords: "
                                       "X=%s(addr=%d) Y=%s(addr=%d) Z=%s(addr=%d) tri=%u "
                                       "stack[%d]=[%s]",
                                       e, info.symName,
                                       xIsPshm ? "PSHM" : "lit", (int)info.pshmAddrX,
                                       yIsPshm ? "PSHM" : "lit", (int)info.pshmAddrY,
                                       zIsPshm ? "PSHM" : "lit", (int)info.pshmAddrZ,
                                       (unsigned)tri, pushCount, stkBuf);
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
                    }
                }

                // SET: 2D position from init script (method 0).
                // Primary: 3 stack params (X, Y, triangleId).
                // Fallback: 2 stack params (X, Y) + triangle from opcParam.
                if (opcode == JSM_OP_SET && m == 0 && !info.hasPosition) {
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
                if (opcode == JSM_OP_LADDERUP || opcode == JSM_OP_LADDERDOWN)
                    foundLadder = true;

                // Map transitions.
                if (opcode == JSM_OP_MAPJUMP || opcode == JSM_OP_MAPJUMP3 ||
                    opcode == JSM_OP_DISCJUMP || opcode == JSM_OP_MAPJUMPO ||
                    opcode == JSM_OP_WORLDMAPJUMP) {
                    foundMapjump = true;
                    methodHasMapjump = true;  // v0.07.84: per-method tracking
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
                    else if (opcode == JSM_OP_MAPJUMPO && pushCount >= 4)
                        mapjumpDestField = pushStack[pushCount - 4];
                    else if (opcode == JSM_OP_WORLDMAPJUMP)
                        mapjumpDestField = -2;  // sentinel: goes to world map
                    else if (pushCount >= 1)
                        mapjumpDestField = pushStack[0];  // best guess: oldest push
                    methodMapjumpDest = mapjumpDestField;  // v0.07.84
                }

                // Model assignment / talk.
                if (opcode == JSM_OP_SETMODEL) foundSetmodel = true;
                if (opcode == JSM_OP_SETMODEL && m == 0) foundSetmodelInit = true;  // v0.12.20
                if (opcode == JSM_OP_TALKON)   foundTalkon = true;

                // Door trigger line.
                if (opcode == JSM_OP_DOORLINEON || opcode == JSM_OP_DOORLINEOFF)
                    foundDoorline = true;

                // v0.12.16: SETLINE interaction zone (opcode 0x29).
                // SETLINE takes 7 params: x1, y1, z1, x2, y2, z2, lineIndex.
                // Extract the line coordinates if they're all literals.
                if (opcode == 0x29 && pushCount >= 7) {
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

                // Item pickup.
                if (opcode == JSM_OP_ADDITEM) foundAdditem = true;

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
                if (opcode == JSM_OP_BATTLE) foundBattle = true;

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
                // REQ pops 3 values: entity_id, method_id, priority.
                // We record the target so we can check if it contains MAPJUMP.
                if ((opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) &&
                    pushCount >= 3 && e < 128) {
                    int reqTargetEnt  = pushStack[pushCount - 3];
                    int reqTargetMeth = pushStack[pushCount - 2];
                    // Validate: target entity must be a valid JSM index, method must be non-negative.
                    if (reqTargetEnt >= 0 && reqTargetEnt < totalEntities &&
                        reqTargetMeth >= 0 && reqTargetMeth < 100 &&
                        s_entityReqs[e].count < MAX_REQ_PER_ENTITY) {
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetEntity = reqTargetEnt;
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetMethod = reqTargetMeth;
                        s_entityReqs[e].count++;
                    }
                    // Model stack effect: REQ pops 3 values.
                    if (pushCount >= 3) pushCount -= 3;
                }

                // v0.07.72: Removed the old pushCount=0 flush here.
                // Stack effects are now modeled per-opcode above.
                // The old flush wiped the dispatch index for 0x1C calls
                // that followed PSHM_W (runtime memory push) instructions.
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

        // v0.07.82: Classify Line entities by opcode signatures.
        // Priority: dialog/interactive > MAPJUMP > battle > event > camera pan > default.
        // v0.12.24: Run for ALL category-1 entities regardless of general classification.
        // Line entities often have MAPJUMP (general block sets MAP_EXIT) AND dialog opcodes
        // (e.g. bgryo1_4 'squall' handles both room exit and uniform interaction).
        // The Line-specific block must override to get the correct line type.
        if (info.jsmCategory == 1) {
            if (foundDialogOp) {
                info.type = JSM_ENT_LINE_INTERACTIVE;
            } else if (info.type == JSM_ENT_LINE_TRIGGER || info.type == JSM_ENT_MAP_EXIT) {
                if (foundMapjump) {
                    info.type = JSM_ENT_LINE_SCREEN_BOUND;
                } else if (foundBattle) {
                    info.type = JSM_ENT_LINE_EVENT;
                } else if (foundEventOp) {
                    info.type = JSM_ENT_LINE_EVENT;
                } else if (foundBgdraw || foundScroll) {
                    info.type = JSM_ENT_LINE_CAMERA_PAN;
                } else {
                    info.type = JSM_ENT_LINE_CAMERA_PAN;
                }
            }
        }

        // v0.12.24: Store ext dispatch flag for dual-purpose Line detection.
        info.hasExtDispatch = foundExtDispatch;

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
            info.type = JSM_ENT_INTERACTIVE_OBJECT;
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
            !strstr(info.symName, "light")) {
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

        outCount++;
    }

    // v0.12.09: Draw point trigger cross-reference.
    // For each entity that calls REQSW/REQEW to a draw point entity,
    // mark it as a draw point trigger. This deterministically links the
    // visible interaction entity (with talkonoff/model) to the invisible
    // draw point script entity (with DRAWPOINT opcode).
    for (int e2 = 0; e2 < outCount; e2++) {
        outEntities[e2].drawPointTriggerOf = -1;  // initialize
        int jsmIdx = outEntities[e2].jsmIndex;
        if (jsmIdx >= 128) continue;
        for (int r = 0; r < s_entityReqs[jsmIdx].count; r++) {
            int tgtEnt = s_entityReqs[jsmIdx].calls[r].targetEntity;
            // Find the target entity in our output array.
            for (int t = 0; t < outCount; t++) {
                if (outEntities[t].jsmIndex == tgtEnt &&
                    outEntities[t].type == JSM_ENT_DRAW_POINT) {
                    outEntities[e2].drawPointTriggerOf = tgtEnt;
                    Log::Field("FieldArchive: [JSMScan] Draw point trigger: ent%d '%s' "
                               "calls draw point ent%d '%s'",
                               jsmIdx, outEntities[e2].symName,
                               tgtEnt, outEntities[t].symName);
                    break;
                }
            }
            if (outEntities[e2].drawPointTriggerOf >= 0) break;
        }
    }

    // v0.16.3: Director-dispatched interaction detection (extracted to director.inl).
    // See field_archive_jsm_director.inl for the full implementation. This call
    // replaces the inline DIAGNOSTIC + Director-detection-post-pass blocks that
    // used to live here.
    RunDirectorDetection(fieldName, outEntities, outCount,
                         countDoors, countLines, countBg,
                         symNames, symCount);

    // v0.07.88: Diagnostic — log POPM_W addresses for unclassified "Other" entities.
    // This helps identify which memory addresses real exit entities write to.
    // v0.12.11: DISABLED — served its purpose, clutters log.
    if (false)
    for (int e2 = 0; e2 < outCount && e2 < 128; e2++) {
        const JSMEntityInfo& ei = outEntities[e2];
        if (ei.jsmCategory != 3) continue;  // only Others
        if (ei.type != JSM_ENT_UNKNOWN && ei.type != JSM_ENT_NPC) continue;  // only unclassified
        if (s_entityPopms[ei.jsmIndex].count == 0) continue;
        char addrBuf[256] = {};
        int pos = 0;
        for (int p = 0; p < s_entityPopms[ei.jsmIndex].count && pos < 240; p++)
            pos += snprintf(addrBuf + pos, 256 - pos, "%d ", (int)s_entityPopms[ei.jsmIndex].addrs[p]);
        Log::Field("FieldArchive: [JSMScan] POPM_W diag: ent%d '%s' type=%s writes=[%s]",
                   ei.jsmIndex, ei.symName, JSMEntityTypeName(ei.type), addrBuf);
    }

    // v0.07.99: Diagnostic — log entities with PSHM_W coordinate markers.
    // These are entities whose SET3 coordinates come from runtime memory variables.
    // The logged addresses tell us which game memory holds X/Y/Z for each entity.
    // v0.12.11: DISABLED — served its purpose, clutters log.
    if (false)
    for (int e2 = 0; e2 < outCount; e2++) {
        const JSMEntityInfo& ei = outEntities[e2];
        if (!ei.hasPshmCoords) continue;
        Log::Field("FieldArchive: [SET3-DIAG] SUMMARY ent%d '%s' cat=%d type=%s "
                   "pshmAddr X=%d Y=%d Z=%d tri=%u litX=%d litY=%d litZ=%d",
                   ei.jsmIndex, ei.symName, ei.jsmCategory,
                   JSMEntityTypeName(ei.type),
                   (int)ei.pshmAddrX, (int)ei.pshmAddrY, (int)ei.pshmAddrZ,
                   (unsigned)ei.posTriangle,
                   (int)ei.posX, (int)ei.posY, (int)ei.posZ);
    }

    // v0.07.89: Diagnostic — dump ALL s_methodMapjumps entries for the field.
    // v0.12.11: DISABLED — served its purpose, clutters log.
    if (false)
    {
        int mjCount = 0;
        for (int mi = 0; mi < totalMethods && mi < MAX_METHOD_MAPJUMPS; mi++) {
            if (!s_methodMapjumps[mi].found) continue;
            mjCount++;
            // Find which entity owns this method by checking group boundaries.
            int ownerEnt = -1;
            for (int oe = 0; oe < totalEntities; oe++) {
                int mStart = groups[oe].startMethodIdx;
                int mEnd   = mStart + groups[oe].methodCount;  // inclusive range is [mStart..mEnd]
                if (mi >= mStart && mi <= mEnd) {
                    ownerEnt = oe;
                    break;
                }
            }
            int ownerSym = ownerEnt - countDoors;
            const char* ownerName = (ownerSym >= 0 && ownerSym < symCount) ? symNames[ownerSym] : "?";
            // Build PSHM_W address list.
            char pshmBuf[256] = {};
            int pp = 0;
            for (int r = 0; r < s_methodMapjumps[mi].pshmCount && pp < 240; r++)
                pp += snprintf(pshmBuf + pp, 256 - pp, "%d ", (int)s_methodMapjumps[mi].pshmAddrs[r]);
            Log::Field("FieldArchive: [JSMScan] MAPJUMP-method diag: method=%d owner=ent%d '%s' dest=%d pshm=[%s]",
                       mi, ownerEnt, ownerName, s_methodMapjumps[mi].destFieldId, pshmBuf);
        }
        Log::Field("FieldArchive: [JSMScan] MAPJUMP-method diag: %d methods with MAPJUMP out of %d total",
                   mjCount, totalMethods);
    }

    // v0.07.93: INF gateway diagnostic using correct Deling format.
    // v0.12.11: DISABLED — served its purpose, clutters log.
    if (false)
    {
        std::vector<uint8_t> infDiag;
        if (ExtractInnerFile(fieldName, ".inf", infDiag) && infDiag.size() >= 676) {
            const uint8_t* infBase = infDiag.data();
            // Log field name from INF header (first 9 bytes).
            char infName[10] = {};
            memcpy(infName, infBase, 9);
            Log::Field("FieldArchive: [INF-DIAG] '%s' size=%d infName='%s'",
                       fieldName, (int)infDiag.size(), infName);
            for (int gi = 0; gi < 12; gi++) {
                const uint8_t* gw = infBase + 0x64 + gi * 32;
                int16_t  x1 = *(const int16_t*)(gw + 0);
                int16_t  y1 = *(const int16_t*)(gw + 2);
                int16_t  z1 = *(const int16_t*)(gw + 4);
                int16_t  x2 = *(const int16_t*)(gw + 6);
                int16_t  y2 = *(const int16_t*)(gw + 8);
                int16_t  z2 = *(const int16_t*)(gw + 10);
                // Destination point (spawn position in target field)
                int16_t  dx = *(const int16_t*)(gw + 12);
                int16_t  dy = *(const int16_t*)(gw + 14);
                int16_t  dz = *(const int16_t*)(gw + 16);
                uint16_t destId = *(const uint16_t*)(gw + 18);
                const char* destName = (destId < 0x7FFF && destId < (uint16_t)s_fieldNames.size())
                                       ? GetFieldNameById(destId) : nullptr;
                float cx = (float)(x1 + x2) / 2.0f;
                float cy = (float)(y1 + y2) / 2.0f;
                Log::Field("FieldArchive: [INF-DIAG] gw[%d] line=(%d,%d,%d)->(%d,%d,%d) center=(%.0f,%.0f) "
                           "dest=(%d,%d,%d) fieldId=%u '%s'",
                           gi, (int)x1, (int)y1, (int)z1, (int)x2, (int)y2, (int)z2,
                           cx, cy, (int)dx, (int)dy, (int)dz,
                           (unsigned)destId,
                           destName ? destName : (destId == 0xFFFF ? "UNUSED" : (destId == 0x7FFF ? "SENTINEL" : "?")));
            }
        } else {
            Log::Field("FieldArchive: [INF-DIAG] no INF or too small for '%s' (size=%d)",
                       fieldName, infDiag.empty() ? 0 : (int)infDiag.size());
        }
    }

    // v0.12.24: REQ-following for Line entity interaction detection.
    // If a Line entity REQs another entity that has dialog opcodes or ext dispatch,
    // the Line is dual-purpose (exit + interaction). Mark it with hasExtDispatch
    // so the catalog Interaction section can detect it.
    for (int i = 0; i < outCount; i++) {
        if (outEntities[i].jsmCategory != 1) continue;  // Line entities only
        if (outEntities[i].hasExtDispatch) continue;     // already flagged
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
            outEntities[i].hasExtDispatch = true;
            Log::Field("FieldArchive: [JSMScan] REQ-interact: Line ent%d '%s' REQs interactive entity -> hasExtDispatch=1",
                       e, outEntities[i].symName);
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

    return true;
}
