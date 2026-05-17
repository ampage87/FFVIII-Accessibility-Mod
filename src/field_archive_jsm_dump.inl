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
