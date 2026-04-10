// field_nav_diagnostics.inl — DumpPshmFunctions, PollDescriptorTable
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

static void DumpPshmFunctions()
{
    if (s_pshmFuncDumpDone) return;
    s_pshmFuncDumpDone = true;

    struct DumpTarget {
        const char* name;
        uint32_t addr;
        int size;
    };
    // v0.08.25: Primary target is the PSHM_W handler from the dispatch table.
    // This is the function that actually resolves shared memory reads.
    // Also dump the surrounding POPM_W/PSHM area for call context.
    DumpTarget targets[] = {
        { "pshm_w_handler",         0x0051C5C0, 1024 },  // THE KEY TARGET
        { "entity_scope_curve_sub", 0x00532890,  512 },
        { "type_clamp_dispatch",    0x0051C9C0,  512 },
    };

    for (int t = 0; t < 3; t++) {
        const char* name = targets[t].name;
        uint32_t addr = targets[t].addr;
        int size = targets[t].size;

        Log::Field("FieldNavigation: [FUNCDUMP] === %s @ 0x%08X (%d bytes) ===", name, addr, size);

        __try {
            const uint8_t* code = (const uint8_t*)addr;
            // Log in 32-byte lines: "ADDR: XX XX XX XX ... (32 bytes)"
            for (int offset = 0; offset < size; offset += 32) {
                int lineLen = size - offset;
                if (lineLen > 32) lineLen = 32;
                char hexBuf[200];
                int pos = 0;
                for (int b = 0; b < lineLen; b++) {
                    pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", code[offset + b]);
                }
                Log::Field("FieldNavigation: [FUNCDUMP] %08X: %s", addr + offset, hexBuf);
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("FieldNavigation: [FUNCDUMP] EXCEPTION reading %s @ 0x%08X", name, addr);
        }
    }

    // Also log what the dispatch table currently holds for PSHM_W (opcode 0x0C)
    // to confirm whether FFNx has replaced it.
    if (FF8Addresses::pExecuteOpcodeTable) {
        uint32_t tableEntry = FF8Addresses::pExecuteOpcodeTable[0x0C];
        Log::Field("FieldNavigation: [FUNCDUMP] dispatch_table[0x0C] (PSHM_W) = 0x%08X %s",
                   tableEntry,
                   (tableEntry == 0x0051C5C0) ? "(matches hardcoded addr)" :
                   (tableEntry > 0x00600000) ? "(likely FFNx replacement)" : "(game code range)");
        // If FFNx replaced it, also dump the original game code at nearby addresses.
        // The original PSHM_W handler should be near the other opcode handlers (~0x0051xxxx).
        if (tableEntry != 0x0051C5C0 && tableEntry > 0x00600000) {
            Log::Field("FieldNavigation: [FUNCDUMP] FFNx hook detected. Dumping FFNx handler too.");
            __try {
                const uint8_t* code = (const uint8_t*)tableEntry;
                Log::Field("FieldNavigation: [FUNCDUMP] === ffnx_pshm_w_hook @ 0x%08X (256 bytes) ===", tableEntry);
                for (int offset = 0; offset < 256; offset += 32) {
                    char hexBuf[200];
                    int pos = 0;
                    for (int b = 0; b < 32; b++)
                        pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", code[offset + b]);
                    Log::Field("FieldNavigation: [FUNCDUMP] %08X: %s", tableEntry + offset, hexBuf);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [FUNCDUMP] EXCEPTION reading FFNx hook");
            }
        }
    }

    Log::Field("FieldNavigation: [FUNCDUMP] === dump complete ===");
}

// ============================================================================
// v0.08.23: Descriptor table polling probe
// ============================================================================
// Scans the per-entity descriptor pointer table at 0x01DCB340 for non-NULL
// entries. Each entry is a pointer to a ~0x90-byte descriptor struct that
// contains the entity-scope computed coordinates at +0x0C/+0x0E.
// Runs for 10 seconds after field load, checking every ~1 second.

static void PollDescriptorTable()
{
    if (!s_descriptorPollActive) return;
    DWORD now = GetTickCount();

    // Check if polling window has expired.
    if (now - s_descriptorPollStart > DESCRIPTOR_POLL_DURATION_MS) {
        if (!s_descriptorPollSummaryLogged) {
            s_descriptorPollSummaryLogged = true;
            Log::Field("FieldNavigation: [DESCPOLL] Polling complete after %d checks, %dms",
                       s_descriptorPollCount, (int)(now - s_descriptorPollStart));
        }
        s_descriptorPollActive = false;
        return;
    }

    // Throttle to DESCRIPTOR_POLL_INTERVAL_MS.
    if (now - s_descriptorPollLastCheck < DESCRIPTOR_POLL_INTERVAL_MS) return;
    s_descriptorPollLastCheck = now;
    s_descriptorPollCount++;

    int totalEnts = s_jsmDoors + s_jsmLines + s_jsmBackgrounds + s_jsmOthers;
    if (totalEnts <= 0 || totalEnts > MAX_DESCRIPTOR_SCAN) {
        Log::Field("FieldNavigation: [DESCPOLL] #%d totalEnts=%d (skipped, out of range 1-%d)",
                   s_descriptorPollCount, totalEnts, MAX_DESCRIPTOR_SCAN);
        return;
    }

    __try {
        uint32_t* table = (uint32_t*)DESCRIPTOR_TABLE_ADDR;
        int foundCount = 0;

        for (int i = 0; i < totalEnts; i++) {
            uint32_t descPtr = table[i];
            if (descPtr == 0) continue;

            // Non-NULL descriptor found — read key fields.
            foundCount++;
            __try {
                uint8_t* desc = (uint8_t*)descPtr;
                int32_t  validity = *(int32_t*)(desc + 0x00);
                int16_t  coordX   = *(int16_t*)(desc + 0x0C);
                int16_t  coordY   = *(int16_t*)(desc + 0x0E);
                uint32_t curvePtr = *(uint32_t*)(desc + 0x68);
                int16_t  cacheKey = *(int16_t*)(desc + 0x7E);

                // Dump first 16 bytes of descriptor for analysis.
                uint8_t hdr[16];
                memcpy(hdr, desc, 16);

                // Get SYM name for this flat entity index.
                const char* symName = (i < s_symNameCount) ? s_symNames[i] : "?";

                Log::Field("FieldNavigation: [DESCPOLL] #%d ent%d '%s' ptr=0x%08X "
                           "valid=%d coords=(%d,%d) curve=0x%08X cacheKey=%d "
                           "hdr=[%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X]",
                           s_descriptorPollCount, i, symName, descPtr,
                           validity, (int)coordX, (int)coordY, curvePtr, (int)cacheKey,
                           hdr[0], hdr[1], hdr[2], hdr[3],
                           hdr[4], hdr[5], hdr[6], hdr[7],
                           hdr[8], hdr[9], hdr[10], hdr[11],
                           hdr[12], hdr[13], hdr[14], hdr[15]);
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [DESCPOLL] #%d ent%d ptr=0x%08X EXCEPTION reading descriptor",
                           s_descriptorPollCount, i, descPtr);
            }
        }

        if (foundCount == 0) {
            Log::Field("FieldNavigation: [DESCPOLL] #%d all %d descriptor ptrs NULL (elapsed %dms)",
                       s_descriptorPollCount, totalEnts, (int)(now - s_descriptorPollStart));
        } else {
            Log::Field("FieldNavigation: [DESCPOLL] #%d found %d/%d non-NULL descriptors",
                       s_descriptorPollCount, foundCount, totalEnts);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [DESCPOLL] #%d EXCEPTION reading descriptor table at 0x%08X",
                   s_descriptorPollCount, DESCRIPTOR_TABLE_ADDR);
        s_descriptorPollActive = false;
    }
}

// v0.12.22: Varblock poller — automatic polling of shared memory varblock
// after field load. Reads varblock[0..799] as int16 every 1s for 10s.
// When the Director entity activates (normal gameplay door transition),
// its POPM_W writes populate interaction zone positions. We capture them here.
// Confirmed by disassembly: PSHM_W(param) = read uint16 at 0x1CFE9B8 + param (byte offset).
static const uint32_t VARBLOCK_BASE_ADDR = 0x1CFE9B8;
static const int VARBLOCK_POLL_RANGE = 800;  // scan first 800 bytes as int16
static const DWORD VARBLOCK_POLL_DURATION_MS = 10000;
static const DWORD VARBLOCK_POLL_INTERVAL_MS = 1000;

static void PollVarblock()
{
    if (!s_varblockPollActive) return;

    DWORD now = GetTickCount();

    // Duration expired?
    if (now - s_varblockPollStart > VARBLOCK_POLL_DURATION_MS) {
        Log::Field("FieldNavigation: [VBPOLL] === COMPLETE === %d polls done",
                   s_varblockPollCount);
        s_varblockPollActive = false;
        return;
    }

    // Interval check
    if (now - s_varblockPollLastCheck < VARBLOCK_POLL_INTERVAL_MS) return;
    s_varblockPollLastCheck = now;
    s_varblockPollCount++;

    __try {
        const uint8_t* vbBase = (const uint8_t*)VARBLOCK_BASE_ADDR;
        int nonZero = 0;
        char summary[2048] = {};
        int sp = 0;

        for (int offset = 0; offset < VARBLOCK_POLL_RANGE; offset += 2) {
            int16_t val = *(const int16_t*)(vbBase + offset);
            if (val != 0) {
                nonZero++;
                if (sp < 1900)
                    sp += snprintf(summary + sp, 2048 - sp, "%d=%d ", offset, (int)val);
            }
        }

        Log::Field("FieldNavigation: [VBPOLL] poll#%d t=+%ds nonZero=%d%s",
                   s_varblockPollCount,
                   (int)((now - s_varblockPollStart) / 1000),
                   nonZero,
                   nonZero > 0 ? " FOUND VALUES" : "");
        if (nonZero > 0)
            Log::Field("FieldNavigation: [VBPOLL]   %s", summary);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [VBPOLL] Exception reading varblock");
        s_varblockPollActive = false;
    }
}

