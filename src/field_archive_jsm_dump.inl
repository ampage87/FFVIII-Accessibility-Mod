// field_archive_jsm_dump.inl — DumpEntityScript script-dump diagnostic.
// Included from field_archive_jsm.inl. Do not compile independently.

bool DumpEntityScript(const char* fieldName, int jsmEntityIndex)
{
    if (!s_initialized) return false;

    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        Log::Field("FieldArchive: [SCRIPT-DUMP] Failed to extract JSM for '%s'", fieldName);
        return false;
    }
    if (jsmData.size() < 8) return false;

    // Parse header
    int countDoors = jsmData[0];
    int countLines = jsmData[1];
    int countBg    = jsmData[2];
    uint16_t posFirst   = *(const uint16_t*)(jsmData.data() + 4);
    uint16_t posScripts = *(const uint16_t*)(jsmData.data() + 6);
    int totalEntities = ((int)posFirst - 8) / 2;

    if (jsmEntityIndex < 0 || jsmEntityIndex >= totalEntities) {
        Log::Field("FieldArchive: [SCRIPT-DUMP] Entity %d out of range (total=%d)", jsmEntityIndex, totalEntities);
        return false;
    }

    // Load SYM names for the entity name
    char symNames[128][32] = {};
    int symCount = 0;
    LoadSYMNames(fieldName, symNames, 128, symCount);
    int symIdx = jsmEntityIndex - countDoors;
    const char* entName = (symIdx >= 0 && symIdx < symCount) ? symNames[symIdx] : "?";

    // Parse entity group entry
    uint16_t groupEntry = *(const uint16_t*)(jsmData.data() + 8 + jsmEntityIndex * 2);
    int methodCount    = groupEntry & 0x7F;
    int startMethodIdx = (int)(groupEntry >> 7);

    // Entry point table and script data
    int totalMethods = (int)(posScripts - posFirst) / 2;
    const uint16_t* entryPoints = (const uint16_t*)(jsmData.data() + posFirst);
    const uint32_t* scriptData  = (const uint32_t*)(jsmData.data() + posScripts);
    int scriptDataDwords = (int)(jsmData.size() - posScripts) / 4;

    // Determine entity category
    const char* cat = "Other";
    if (jsmEntityIndex < countDoors) cat = "Door";
    else if (jsmEntityIndex < countDoors + countLines) cat = "Line";
    else if (jsmEntityIndex < countDoors + countLines + countBg) cat = "Background";

    Log::Field("FieldArchive: [SCRIPT-DUMP] === Entity %d '%s' (%s) on '%s' ===",
               jsmEntityIndex, entName, cat, fieldName);
    Log::Field("FieldArchive: [SCRIPT-DUMP] methods=%d startMethodIdx=%d fileSize=%d posFirst=%d posScripts=%d scriptDataDwords=%d",
               methodCount, startMethodIdx, (int)jsmData.size(), (int)posFirst, (int)posScripts, scriptDataDwords);

    // Iterate through all methods (0 = init, 1+ = per-frame/interaction)
    for (int m = 0; m <= methodCount; m++) {
        int methodIdx = startMethodIdx + m;
        if (methodIdx >= totalMethods) break;

        uint16_t scriptStart = entryPoints[methodIdx] & 0x7FFF;  // v0.12.23: mask bit15 flag
        uint16_t scriptEnd   = (uint16_t)scriptDataDwords;
        if (methodIdx + 1 < totalMethods)
            scriptEnd = entryPoints[methodIdx + 1] & 0x7FFF;  // v0.12.23: mask bit15

        int instrCount = (int)scriptEnd - (int)scriptStart;
        if (instrCount <= 0) {
            Log::Field("FieldArchive: [SCRIPT-DUMP]   method[%d] (empty)", m);
            continue;
        }
        Log::Field("FieldArchive: [SCRIPT-DUMP]   method[%d] dwords %d-%d (%d instructions):",
                   m, (int)scriptStart, (int)scriptEnd - 1, instrCount);

        // v0.12.23: Bounds check diagnostic
        if ((int)scriptStart >= scriptDataDwords) {
            Log::Field("FieldArchive: [SCRIPT-DUMP]   SKIPPED: scriptStart=%d >= scriptDataDwords=%d (file too small or uint16 overflow)",
                       (int)scriptStart, scriptDataDwords);
            continue;
        }

        // Decode instructions
        int pushStack[16] = {};
        int pushCount = 0;
        for (int ip = (int)scriptStart; ip < (int)scriptEnd && ip < scriptDataDwords; ip++) {
            uint32_t word = scriptData[ip];
            uint8_t highByte = (uint8_t)(word >> 24);

            if (highByte == 0) {
                // Push literal
                int32_t val = (int32_t)word;
                if (pushCount < 16) pushStack[pushCount++] = val;
                Log::Field("FieldArchive: [SCRIPT-DUMP]     [%4d] PUSH %d (0x%06X)",
                           ip, val, (unsigned)word);
            } else {
                // Opcode
                int32_t param = (int32_t)(word & 0x00FFFFFF);
                if (word & 0x00800000) param |= (int32_t)0xFF000000;

                // Handle 0x1C extended dispatch
                uint16_t effectiveOp = highByte;
                const char* extNote = "";
                char extBuf[64] = {};
                if (highByte == 0x1C && pushCount > 0) {
                    int32_t extOp = pushStack[--pushCount];
                    if (extOp >= 0 && extOp < 0x200) {
                        effectiveOp = (uint16_t)extOp;
                        snprintf(extBuf, sizeof(extBuf), " (ext dispatch -> 0x%03X)", (unsigned)extOp);
                        extNote = extBuf;
                    } else {
                        snprintf(extBuf, sizeof(extBuf), " (ext dispatch -> PSHM 0x%08X)", (unsigned)extOp);
                        extNote = extBuf;
                    }
                }

                const char* opName = GetOpcodeName(effectiveOp);
                char nameBuf[32];
                if (!opName) {
                    snprintf(nameBuf, sizeof(nameBuf), "OP_0x%03X", (unsigned)effectiveOp);
                    opName = nameBuf;
                }

                // Build stack context string (last 4 pushes)
                char stkBuf[128] = {};
                if (pushCount > 0) {
                    int sp = 0;
                    int start = (pushCount > 4) ? pushCount - 4 : 0;
                    sp += snprintf(stkBuf + sp, 128 - sp, " stk[");
                    for (int s = start; s < pushCount && sp < 120; s++)
                        sp += snprintf(stkBuf + sp, 128 - sp, "%d ", pushStack[s]);
                    sp += snprintf(stkBuf + sp, 128 - sp, "]");
                }

                Log::Field("FieldArchive: [SCRIPT-DUMP]     [%4d] %s param=%d%s%s",
                           ip, opName, param, extNote, stkBuf);

                // Model stack effects for PSHM_W
                if (highByte == 0x07 || highByte == 0x09 || highByte == 0x0A ||
                    highByte == 0x0C || highByte == 0x0D) {
                    int32_t marker;
                    if (highByte == 0x07 && param < 0) {
                        marker = param; // passthrough literal
                    } else {
                        marker = (int32_t)(0x80000000u | (uint32_t)(param & 0xFFFF));
                    }
                    if (pushCount < 16) pushStack[pushCount++] = marker;
                } else if (highByte == 0x02 || highByte == 0x08 || highByte == 0x0B) {
                    if (pushCount > 0) pushCount--;
                } else if (highByte != 0x01 && highByte != 0x03 && highByte != 0x04 &&
                           highByte != 0x05 && highByte != 0x06 && highByte != 0x1C) {
                    // Unknown stack effect — don't flush
                }
            }
        }
    }

    Log::Field("FieldArchive: [SCRIPT-DUMP] === End entity %d '%s' ===", jsmEntityIndex, entName);
    return true;
}

// v0.18.3.2: Train code-apparatus script dump (#56). Dumps the JSM opcode
// stream of the Timber-train code entities so the uncoupling code's storage
// can be found statically: the random-code generation, the POPM_W stores
// (the varblock addresses the 4 code digits live at), and the draw-number
// opcode. The digits are sprite-drawn (NOT in any window text buffer --
// confirmed by exe disassembly of the field text engine, which has no
// inline number-from-variable control code), so the announcement must read
// the values from the varblock; this dump locates the addresses.
//
// Strategy: dump every entity whose SYM name starts with "ango" (Angoyarukun,
// the code apparatus / "code guy") or "key" (Keykantoku / Keyjokantoku key
// supervisors). If none match, fall back to dumping every "Other" entity so
// the apparatus is captured regardless of naming. Reuses DumpEntityScript.
// Log-only; fires once per field entry (caller-gated). -> ff8_field.log.
bool DumpTrainCodeScripts(const char* fieldName)
{
    if (!s_initialized) return false;

    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        Log::Field("FieldArchive: [SCRIPT-DUMP] Train: failed to extract JSM for '%s'", fieldName);
        return false;
    }
    if (jsmData.size() < 8) return false;

    int countDoors = jsmData[0];
    int countLines = jsmData[1];
    int countBg    = jsmData[2];
    uint16_t posFirst = *(const uint16_t*)(jsmData.data() + 4);
    int totalEntities = ((int)posFirst - 8) / 2;
    int firstOther = countDoors + countLines + countBg;

    char symNames[128][32] = {};
    int symCount = 0;
    LoadSYMNames(fieldName, symNames, 128, symCount);

    Log::Field("FieldArchive: [SCRIPT-DUMP] === Train code dump '%s': %d entities "
               "(D=%d L=%d B=%d firstOther=%d) %d SYM names ===",
               fieldName, totalEntities, countDoors, countLines, countBg, firstOther, symCount);

    int matched = 0;
    for (int symIdx = 0; symIdx < symCount; symIdx++) {
        const char* nm = symNames[symIdx];
        if (_strnicmp(nm, "ango", 4) == 0 || _strnicmp(nm, "key", 3) == 0) {
            int jsmEntityIndex = symIdx + countDoors;
            if (jsmEntityIndex >= 0 && jsmEntityIndex < totalEntities) {
                Log::Field("FieldArchive: [SCRIPT-DUMP] Train: name match '%s' -> jsmIndex %d",
                           nm, jsmEntityIndex);
                DumpEntityScript(fieldName, jsmEntityIndex);
                matched++;
            }
        }
    }

    if (matched == 0) {
        Log::Field("FieldArchive: [SCRIPT-DUMP] Train: no ango/key name match; "
                   "dumping all %d 'Other' entities", totalEntities - firstOther);
        for (int e = firstOther; e < totalEntities; e++) {
            DumpEntityScript(fieldName, e);
            matched++;
        }
    }

    Log::Field("FieldArchive: [SCRIPT-DUMP] === Train code dump '%s' complete: %d dumped ===",
               fieldName, matched);
    return matched > 0;
}

// v0.18.3.9: Guard + controller script dump for the Timber train (#58).
// On tilink1 the patrolling guards are GalHei1/GalHei2 (ents 5/6); the
// "spotted -> restart" logic is either in their (light, 3-method) scripts or
// in a controller. Dump galhei* + the prime controller candidates
// (TrainSindou, point) so the patrol MOVE loop, the line-of-sight/proximity
// check, and the catch trigger (MAPJUMP to a caught field / a fail-flag POPM)
// can be read statically. Reuses DumpEntityScript. Log-only -> ff8_field.log.
bool DumpGuardScripts(const char* fieldName)
{
    if (!s_initialized) return false;

    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        Log::Field("FieldArchive: [SCRIPT-DUMP] Guard: failed to extract JSM for '%s'", fieldName);
        return false;
    }
    if (jsmData.size() < 8) return false;

    int countDoors = jsmData[0];
    uint16_t posFirst = *(const uint16_t*)(jsmData.data() + 4);
    int totalEntities = ((int)posFirst - 8) / 2;

    char symNames[128][32] = {};
    int symCount = 0;
    LoadSYMNames(fieldName, symNames, 128, symCount);

    Log::Field("FieldArchive: [SCRIPT-DUMP] === Guard dump '%s': %d entities, %d SYM names ===",
               fieldName, totalEntities, symCount);

    // v0.18.3.11: dump ALL entities. v0.18.3.10 proved the catch logic is NOT
    // in galhei/trainsindou/point (decorative sprites + the shake controller +
    // a scenery orchestrator). The guard-vs-Squall detection lives elsewhere --
    // most likely a blind* invisible trigger or the train-movement entities --
    // so dump everything and hunt for the proximity/distance check + the catch
    // trigger (MAPJUMP / REQEW-to-caught / fail-flag POPM).
    // v0.18.3.12 (#58): entities 0-17 already analyzed clean -- party/NPC
    // sprites, the train-shake (TrainSindou) + hatch/view/camera controllers,
    // and the Noriuturiline1 code-entry line. None hold a guard-proximity
    // check or the fail-ASK. Dump ONLY 18-30 (blind1..blind10, hatch, point,
    // v0.18.3.13 (#58): entities 18-25 now also mapped (blind1-blind8 = the
    // guards blind2/3 + master blind4 + validators/display). Advance the start
    // to 26 so the LAST unread suspects (blind9, blind10, hatch, point, light)
    // land alone at the TOP of the log -- hunting the fail-ASK (opcode 0x4A,
    // "Rinoa: What happened!?") + any player-distance check not yet found.
    const int kFirstDumpSym = 26;
    int matched = 0;
    for (int symIdx = kFirstDumpSym; symIdx < symCount; symIdx++) {
        const char* nm = symNames[symIdx];
        int jsmEntityIndex = symIdx + countDoors;
        if (jsmEntityIndex >= 0 && jsmEntityIndex < totalEntities) {
            Log::Field("FieldArchive: [SCRIPT-DUMP] entity '%s' -> jsmIndex %d", nm, jsmEntityIndex);
            DumpEntityScript(fieldName, jsmEntityIndex);
            matched++;
        }
    }

    Log::Field("FieldArchive: [SCRIPT-DUMP] === Guard dump '%s' complete: %d dumped ===",
               fieldName, matched);
    return matched > 0;
}
