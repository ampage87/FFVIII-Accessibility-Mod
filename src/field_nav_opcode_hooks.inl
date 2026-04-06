// field_nav_opcode_hooks.inl — SETLINE/TALKRAD/PUSHRAD/SET3/PSHM_W hooks
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// v05.56: SETLINE/LINEON/LINEOFF hooks — capture trigger line coordinates
// ============================================================================
//
// SETLINE(entityPtr) is called by JSM scripts to define a trigger line.
// The entityPtr is the address of the entity state struct. After the
// original handler runs, the line coordinates are stored somewhere in
// that struct. We capture them by dumping the struct.

// CapturedTriggerLine struct, s_capturedLines[], s_capturedLineCount, and
// s_setlineCallCount are declared above (before ComputeAStarPath) in v05.92.

// SETLINE hook: call original, then read line coordinates from entity struct.
// v05.57: Coordinates confirmed at offset 0x188 in the entity struct:
//   0x188: int16 X1, int16 Y1, int16 Z1, int16 X2, int16 Y2, int16 Z2, int16 lineIdx
static const DWORD LINE_COORD_OFFSET = 0x188;

static int __cdecl HookedSetline(int entityPtr)
{
    int result = s_originalSetline(entityPtr);
    s_setlineCallCount++;

    const char* fieldName = FF8Addresses::pCurrentFieldName
                            ? FF8Addresses::pCurrentFieldName : "(null)";

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t x1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 0);
        int16_t y1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 2);
        int16_t z1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 4);
        int16_t x2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 6);
        int16_t y2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 8);
        int16_t z2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 10);
        int16_t lineIdx = *(int16_t*)(ent + LINE_COORD_OFFSET + 12);

        // Store in captured lines array (deduplicate by entity address).
        int slot = -1;
        for (int i = 0; i < s_capturedLineCount; i++) {
            if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr) {
                slot = i;  // update existing
                break;
            }
        }
        if (slot < 0 && s_capturedLineCount < MAX_CAPTURED_LINES)
            slot = s_capturedLineCount++;

        if (slot >= 0) {
            s_capturedLines[slot].entityAddr = (uint32_t)entityPtr;
            // v05.58: SETLINE stores (X,Y,Z) where Y=vertical. For 2D nav
            // we use X (screen-right) and Y (screen-up), not Z (depth).
            s_capturedLines[slot].x1 = x1;
            s_capturedLines[slot].y1 = y1;
            s_capturedLines[slot].z1 = z1;
            s_capturedLines[slot].x2 = x2;
            s_capturedLines[slot].y2 = y2;
            s_capturedLines[slot].z2 = z2;
            s_capturedLines[slot].active = true;  // SETLINE implies active
            s_capturedLines[slot].lineOrder = s_setlineCallCount - 1; // 0-based
            // Name resolved later in RefreshCatalog (SYM not yet loaded here).
            s_capturedLines[slot].name[0] = '\0';
        }

        // v05.58: Center uses X and Y (not Z) for 2D navigation.
        float cx = (float)(x1 + x2) / 2.0f;
        float cy = (float)(y1 + y2) / 2.0f;
        Log::Field("FieldNavigation: [SETLINE] call#%d field=%s ent=0x%08X "
                   "line(%d,%d,%d)->(%d,%d,%d) idx=%d center=(%.0f,%.0f)",
                   s_setlineCallCount, fieldName, (uint32_t)entityPtr,
                   (int)x1, (int)y1, (int)z1, (int)x2, (int)y2, (int)z2,
                   (int)lineIdx, cx, cy);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [SETLINE] call#%d field=%s ent=0x%08X (SEH)",
                   s_setlineCallCount, fieldName, (uint32_t)entityPtr);
    }

    return result;
}

static int __cdecl HookedLineon(int entityPtr)
{
    int result = s_originalLineon(entityPtr);
    for (int i = 0; i < s_capturedLineCount; i++) {
        if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr)
            s_capturedLines[i].active = true;
    }
    Log::Field("FieldNavigation: [LINEON] field=%s ent=0x%08X",
               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
               (uint32_t)entityPtr);
    return result;
}

static int __cdecl HookedLineoff(int entityPtr)
{
    int result = s_originalLineoff(entityPtr);
    for (int i = 0; i < s_capturedLineCount; i++) {
        if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr)
            s_capturedLines[i].active = false;
    }
    Log::Field("FieldNavigation: [LINEOFF] field=%s ent=0x%08X",
               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
               (uint32_t)entityPtr);
    return result;
}

// ============================================================================
// v05.78: TALKRADIUS/PUSHRADIUS hooks — capture interaction radii
// ============================================================================
//
// TALKRADIUS(entityPtr) sets the radius within which the player can talk to
// this entity (by pressing X/Confirm). The radius value was on the JSM stack
// before dispatch and has been consumed by the handler — we read it from the
// entity struct after the handler returns.
//
// The entity struct is the "others" entity state (ff8_field_state_other).
// The entityPtr passed to the opcode handler is the same pointer as
// base + ENTITY_STRIDE * i for the entity executing the opcode.

static int __cdecl HookedTalkradius(int entityPtr)
{
    // v05.79: Capture BEFORE values at candidate offsets, then call original,
    // then capture AFTER. Log only the offsets that changed.
    // Scan 0x188-0x25E = 0xD6 bytes = 107 uint16 slots
    static const int SCAN_START = 0x188;
    static const int SCAN_SLOTS = 107;
    uint16_t before[SCAN_SLOTS] = {};
    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        for (int i = 0; i < SCAN_SLOTS; i++)
            before[i] = *(uint16_t*)(ent + SCAN_START + i * 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    int result = s_originalTalkradius(entityPtr);

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t  modelId = *(int16_t*)(ent + 0x218);

        Log::Field("FieldNavigation: [TALKRAD] ent=0x%08X model=%d",
                   (uint32_t)entityPtr, (int)modelId);

        // Log only offsets that changed (the smoking gun)
        int changedCount = 0;
        for (int i = 0; i < SCAN_SLOTS; i++) {
            uint16_t after = *(uint16_t*)(ent + SCAN_START + i * 2);
            if (after != before[i]) {
                uint32_t off = SCAN_START + i * 2;
                Log::Field("FieldNavigation: [TALKRAD]   CHANGED @0x%03X: %u -> %u",
                           off, (unsigned)before[i], (unsigned)after);
                changedCount++;
            }
        }
        if (changedCount == 0)
            Log::Field("FieldNavigation: [TALKRAD]   NO changes in 0x188-0x25E range!");

        // Also dump the full 0x21A-0x24E region as context
        Log::Field("FieldNavigation: [TALKRAD]   context: @21A=%u @21C=%u @21E=%u @220=%u @222=%u @224=%u @234=%u @236=%u @244=%u @246=%u",
                   *(uint16_t*)(ent+0x21A), *(uint16_t*)(ent+0x21C),
                   *(uint16_t*)(ent+0x21E), *(uint16_t*)(ent+0x220),
                   *(uint16_t*)(ent+0x222), *(uint16_t*)(ent+0x224),
                   *(uint16_t*)(ent+0x234), *(uint16_t*)(ent+0x236),
                   *(uint16_t*)(ent+0x244), *(uint16_t*)(ent+0x246));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [TALKRAD] ent=0x%08X (SEH)", (uint32_t)entityPtr);
    }

    return result;
}

static int __cdecl HookedPushradius(int entityPtr)
{
    // v05.79: Before/after diff, same as TALKRADIUS.
    static const int SCAN_START_P = 0x188;
    static const int SCAN_SLOTS_P = 107;
    uint16_t before[SCAN_SLOTS_P] = {};
    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        for (int i = 0; i < SCAN_SLOTS_P; i++)
            before[i] = *(uint16_t*)(ent + SCAN_START_P + i * 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    int result = s_originalPushradius(entityPtr);

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t  modelId = *(int16_t*)(ent + 0x218);

        // v0.12.11: PUSHRAD verbose logging disabled (served its purpose).
    if (false) {
    Log::Field("FieldNavigation: [PUSHRAD] ent=0x%08X model=%d",
                   (uint32_t)entityPtr, (int)modelId);

        int changedCount = 0;
        for (int i = 0; i < SCAN_SLOTS_P; i++) {
            uint16_t after = *(uint16_t*)(ent + SCAN_START_P + i * 2);
            if (after != before[i]) {
                uint32_t off = SCAN_START_P + i * 2;
                Log::Field("FieldNavigation: [PUSHRAD]   CHANGED @0x%03X: %u -> %u",
                           off, (unsigned)before[i], (unsigned)after);
                changedCount++;
            }
        }
        if (changedCount == 0)
            Log::Field("FieldNavigation: [PUSHRAD]   NO changes in 0x188-0x25E range!");

        Log::Field("FieldNavigation: [PUSHRAD]   context: @21A=%u @21C=%u @21E=%u @220=%u @222=%u @224=%u @234=%u @236=%u @244=%u @246=%u",
                   *(uint16_t*)(ent+0x21A), *(uint16_t*)(ent+0x21C),
                   *(uint16_t*)(ent+0x21E), *(uint16_t*)(ent+0x220),
                   *(uint16_t*)(ent+0x222), *(uint16_t*)(ent+0x224),
                   *(uint16_t*)(ent+0x234), *(uint16_t*)(ent+0x236),
                   *(uint16_t*)(ent+0x244), *(uint16_t*)(ent+0x246));
    } // end v0.12.11 PUSHRAD diagnostic disable
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [PUSHRAD] ent=0x%08X (SEH)", (uint32_t)entityPtr);
    }

    return result;
}

// ============================================================================
// v0.08.03: SET3 opcode hook — capture entity positions at runtime
// ============================================================================
// Fires during field_scripts_init when any entity executes SET3 (opcode 0x1E).
// After calling the original handler, we read the entity’s resolved position
// from the entity state struct. This captures PSHM_W-sourced coordinates that
// the static JSM scanner can’t resolve (e.g. bghall_1 Directory panel).

// v0.09.38: SET3 capture logic extracted from the hook to avoid SEH/stack overhead.
// Called from HookedSet3 only when capturing is active.
// This function is NOT on the game's script interpreter call path — safe to use SEH.
static void CaptureSet3Position(uint32_t entityAddr)
{
    s_set3TotalCalls++;

    if (s_set3CaptureCount < MAX_SET3_CAPTURES) {
        __try {
            uint8_t* ent = (uint8_t*)entityAddr;
            int32_t fpX = *(int32_t*)(ent + 0x190);
            int32_t fpY = *(int32_t*)(ent + 0x194);
            uint16_t tri = *(uint16_t*)(ent + 0x1FA);
            int16_t posX = (int16_t)(fpX / 4096);
            int16_t posY = (int16_t)(fpY / 4096);
            if (fpX == 0 && fpY == 0) {
                posX = *(int16_t*)(ent + 0x20);
                posY = *(int16_t*)(ent + 0x24);
            }
            int slot = -1;
            for (int c = 0; c < s_set3CaptureCount; c++) {
                if (s_set3Captures[c].entityAddr == entityAddr) {
                    slot = c;
                    break;
                }
            }
            bool isNew = (slot < 0);
            if (isNew) slot = s_set3CaptureCount++;
            s_set3Captures[slot].entityAddr = entityAddr;
            s_set3Captures[slot].posX = posX;
            s_set3Captures[slot].posY = posY;
            s_set3Captures[slot].posZ = 0;
            s_set3Captures[slot].triId = tri;
            if (isNew) {
                Log::Field("FieldNavigation: [SET3-HOOK] NEW ent=0x%08X pos=(%d,%d) tri=%u slot=%d/%d totalCalls=%d",
                           entityAddr, (int)posX, (int)posY,
                           (unsigned)tri, slot, s_set3CaptureCount, s_set3TotalCalls);
                s_set3Captures[slot].firstLogged = true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("FieldNavigation: [SET3-HOOK] ent=0x%08X (SEH)", entityAddr);
        }
    }
    if ((s_set3TotalCalls % 10000) == 0) {
        Log::Field("FieldNavigation: [SET3-HOOK] summary: %d unique entities, %d total calls",
                   s_set3CaptureCount, s_set3TotalCalls);
    }
}

// v0.09.38: Minimal SET3 hook — NO SEH, no locals, pure passthrough.
// The original had __try/__except which installs an SEH frame on the stack.
// The FF8 script interpreter appears to be sensitive to stack frame changes
// in opcode handlers, causing the infirmary scene hang.
static int __cdecl HookedSet3(int entityPtr)
{
    int result = s_originalSet3(entityPtr);
    if (s_capturingSET3)
        CaptureSet3Position((uint32_t)entityPtr);
    return result;
}

// ============================================================================
// v0.08.07: PSHM_W opcode hook — capture shared memory reads at runtime
// ============================================================================
// Fires for every PSHM_W call during field_scripts_init. After the original
// handler runs, we read the value it pushed to the VM stack. This tells us
// what the engine resolves each PSHM_W address to, including for entities
// that use the alternate code path (entity-scope / parametric curves).

static int __cdecl HookedPshmW(int entityPtr)
{
    // Call original handler first — game behaviour unchanged.
    int result = s_originalPshmW(entityPtr);

    // Minimal capture: just count + log entity address. No struct reads.
    // Previous builds crashed when reading entity+0x184/0x140 during per-frame calls.
    if (s_capturingPSHM) {
        s_pshmCaptureCount++;

        // Auto-expire after 5 seconds.
        if ((s_pshmCaptureCount & 0xFF) == 0) {  // check time every 256 calls
            DWORD elapsed = GetTickCount() - s_pshmCaptureStartTime;
            if (elapsed > PSHM_CAPTURE_DURATION_MS) {
                s_capturingPSHM = false;
                Log::Field("FieldNavigation: [PSHM_W-HOOK] Capture window closed: %d calls in %ums",
                           s_pshmCaptureCount, elapsed);
                return result;
            }
        }

        // Log first 20 calls with just entity address (no struct reads).
        if (s_pshmCaptureCount <= 20) {
            Log::Field("FieldNavigation: [PSHM_W-HOOK] #%d ent=0x%08X result=%d",
                       s_pshmCaptureCount, (uint32_t)entityPtr, result);
        }
    }

    return result;
}

