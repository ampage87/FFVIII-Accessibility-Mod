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

// ============================================================================
// v0.14.45: POPM_W/B/L shared memory write capture hooks removed.
// Was used in v0.12.22 for Director varblock investigation (resolved session 43:
// SETLINE triggers are definitive, Director entities are dead code). Retired
// along with the F12 trigger and supporting state.
// ============================================================================

#include "ladder_cue.inl"

// The player's own entity block, or 0 when it cannot be read.
static uint32_t PlayerEntityBlockAddr()
{
    if (!FF8Addresses::pFieldStateOthers || s_playerEntityIdx < 0) return 0;
    uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
    if (!base) return 0;
    return (uint32_t)(uintptr_t)(base + ENTITY_STRIDE * s_playerEntityIdx);
}

// v0.124.0 (#centra): the climb sound, polled rather than hooked.
//
// v0.123.0 hooked the four ladder-move opcodes and played a cue on each. The
// engine's own step routine is better than a cue AND better than a hook: the
// movement mode byte at +0x23C is the engine's own statement that a ladder move
// is in progress, so a poll needs no timer to guess how long a climb lasts and
// no debounce to survive one climb firing several opcodes. Steps play while the
// mode says climbing and stop the frame it clears.
static bool s_ladderClimbingPrev = false;

// v0.126.0-v0.127.0 carried a MODE-WATCH trace here: a first-sighting bitmask
// over every movement mode the engine played a ladder step in, and a table of
// every mode-to-mode transition that was not a climb. It existed to answer one
// question -- what mode does crroof1's descent set? -- and v0.128.0 answered it
// by reading the script instead: **none**, because the descent runs no ladder
// opcode at all. v0.131.8 removes the trace. It had nothing left to say, and it
// was saying it on the engine's step path.

// The player's movement mode, or -1 when the entity block cannot be read. Used
// by the poll below and by the step hook, which has to know whether the step it
// just intercepted was part of a climb or an ordinary footfall on a floor.
static int PlayerMovementMode(uint32_t block)
{
    int mode = -1;
    __try { mode = (int)*(volatile const unsigned char*)(uintptr_t)(block + 0x23C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return mode;
}

// v0.125.0 (#centra): the engine's own step routine, hooked to be MEASURED.
//
// Two jobs, and the second one only exists because the first made it possible.
// It times the gaps between the game's own ladder steps so the mod can replay
// that cadence on the ladders the game leaves silent -- Aaron's "very fast"
// against a 260 ms number this project guessed. And because it sees those steps
// happen, it also tells the poll below to shut up while they are happening,
// which stops v0.124.0 from laying a second set of steps over crtower3's and
// crtower1's ladders that already sound.
//
// Only the player's own steps count, and only while the movement mode says a
// ladder: an NPC walking past, or the player crossing a floor, is a different
// rhythm entirely and would poison the sample.
static LadderStepSoundFn s_originalLadderStep = nullptr;
static void __cdecl HookedLadderStepSound(int entityPtr, int foot, int volume, int pan)
{
    const uint32_t block = PlayerEntityBlockAddr();
    if (block != 0 && (uint32_t)entityPtr == block) {
        const int mode = PlayerMovementMode(block);
        if (LadderModeIsClimb(mode)) {
            const unsigned folded = LadderNoteEngineStep((unsigned)GetTickCount());
            if (folded != 0)
                Log::Field("FieldNavigation: [LADDER] engine step, gap %u ms -> %u ms "
                           "(x%u) -> cadence now %u ms [v0.127.0]",
                           s_ladderLastRawGapMs, folded, s_ladderLastFoldN,
                           s_ladderIntervalMs);
            else if (s_ladderLastRawGapMs != 0)
                Log::Field("FieldNavigation: [LADDER] engine step, gap %u ms folds to "
                           "nothing plausible against %u ms -- not learned from "
                           "[v0.127.0]", s_ladderLastRawGapMs, s_ladderIntervalMs);
        }
    }
    if (s_originalLadderStep) s_originalLadderStep(entityPtr, foot, volume, pan);
}

// Signature-checked BEFORE the hook goes in, because MinHook overwrites the very
// prologue bytes the check reads. This hook only listens -- the cue itself calls
// sub_0046B2A0 directly (v0.131.0) -- so a failed check costs the cadence
// measurement and nothing else: the cue still plays at the measured default.
static void InstallLadderStepHook()
{
    if (!LadderStepSigMatches((uintptr_t)LADDER_STEP_ADDR)) {
        Log::Field("FieldNavigation: [LADDER] step routine @ 0x%08X failed its "
                   "signature check — cadence stays at the %u ms default "
                   "[v0.131.8]", LADDER_STEP_ADDR, s_ladderIntervalMs);
        return;
    }
    MH_STATUS st = MH_CreateHook((LPVOID)(uintptr_t)LADDER_STEP_ADDR,
                                 (LPVOID)HookedLadderStepSound,
                                 (LPVOID*)&s_originalLadderStep);
    if (st == MH_OK) st = MH_EnableHook((LPVOID)(uintptr_t)LADDER_STEP_ADDR);
    Log::Field("FieldNavigation: [LADDER] step routine hook @ 0x%08X — %s "
               "(default cadence %u ms) [v0.125.0]",
               LADDER_STEP_ADDR, MH_StatusToString(st), s_ladderIntervalMs);
}

// v0.130.0: the game's own statement that a ladder move is running.
//
// PREQEW (opcode 0x019, handler 0x0051D530) is what crroof1's ladder lines run
// once BTNTEST says the button is down, and it is a WAIT -- it returns 3 while
// the requested party method is still going, so the interpreter calls it again
// on the very next frame, and returns 1 only when that method has finished. Its
// first argument is the calling entity: the line itself.
//
// The hook does as little as possible on that hot path -- stamp the caller and
// the tick into a small ring -- and every decision about whether that caller is
// a ladder is made on the mod side, below.
typedef int (__cdecl* PreqewHandler_t)(int entityPtr, int param);
static PreqewHandler_t s_originalPreqew = nullptr;

static volatile uint32_t s_preqewEnt[LADDER_PREQEW_SLOTS] = { 0 };
static volatile uint32_t s_preqewMs [LADDER_PREQEW_SLOTS] = { 0 };
static volatile int      s_preqewNext = 0;
static volatile bool     s_preqewEver = false;

static int __cdecl HookedPreqew(int entityPtr, int param)
{
    const uint32_t now = (uint32_t)GetTickCount();
    int slot = -1;
    for (int i = 0; i < LADDER_PREQEW_SLOTS; i++)
        if (s_preqewEnt[i] == (uint32_t)entityPtr) { slot = i; break; }
    if (slot < 0) {
        slot = s_preqewNext;
        s_preqewNext = (s_preqewNext + 1) % LADDER_PREQEW_SLOTS;
        s_preqewEnt[slot] = (uint32_t)entityPtr;
    }
    s_preqewMs[slot] = now;
    s_preqewEver = true;
    return s_originalPreqew ? s_originalPreqew(entityPtr, param) : 1;
}

static void InstallLadderPreqewHook()
{
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        Log::Field("FieldNavigation: [LADDER] no opcode table — PREQEW hook skipped [v0.130.0]");
        return;
    }
    const uint32_t addr = FF8Addresses::pExecuteOpcodeTable[0x19];
    if (addr == 0) return;
    MH_STATUS st = MH_CreateHook((LPVOID)(uintptr_t)addr, (LPVOID)HookedPreqew,
                                 (LPVOID*)&s_originalPreqew);
    if (st == MH_OK) st = MH_EnableHook((LPVOID)(uintptr_t)addr);
    Log::Field("FieldNavigation: [LADDER] PREQEW hook @ 0x%08X — %s [v0.130.0]",
               addr, MH_StatusToString(st));
}

// The ladder line whose move is running right now, or null. Matching is by the
// line's ENTITY ADDRESS, so a catalog refresh mid-move (the descent teleports
// the player into the other camera zone, which re-filters the catalog) cannot
// drop the match: once armed, the address is remembered until the heartbeat
// stops.
static uint32_t s_ladderCueLineAddr = 0;

static const char* LadderPreqewLineRunning(unsigned nowMs)
{
    for (int c = 0; c < s_catalogCount; c++) {
        if (!LadderNameIsLadder(s_catalog[c].name)) continue;
        int slot = -1;
        if (!LadderCatalogIsTriggerLine(s_catalog[c].entityIdx, s_capturedLineCount, &slot))
            continue;
        const uint32_t addr = s_capturedLines[slot].entityAddr;
        for (int i = 0; i < LADDER_PREQEW_SLOTS; i++) {
            if (s_preqewEnt[i] != addr) continue;
            if (LadderPreqewIsRecent(nowMs, s_preqewMs[i], s_preqewEver)) {
                s_ladderCueLineAddr = addr;
                return s_catalog[c].name;
            }
        }
    }
    return 0;
}

static bool LadderPreqewStillRunning(unsigned nowMs)
{
    if (s_ladderCueLineAddr == 0) return false;
    for (int i = 0; i < LADDER_PREQEW_SLOTS; i++) {
        if (s_preqewEnt[i] != s_ladderCueLineAddr) continue;
        return LadderPreqewIsRecent(nowMs, s_preqewMs[i], s_preqewEver);
    }
    return false;
}

static void PollLadderClimbSound()
{
    const uint32_t block = PlayerEntityBlockAddr();
    if (block == 0) return;
    const int mode = PlayerMovementMode(block);
    if (mode < 0) return;
    const unsigned nowMs = (unsigned)GetTickCount();

    // v0.130.0: the cue is exactly as long as the game's own PREQEW wait.
    {
        if (!s_ladderScriptCue) {
            const char* lineName = LadderPreqewLineRunning(nowMs);
            if (lineName != 0) {
                s_ladderScriptCue = true;
                Log::Field("FieldNavigation: [LADDER] scripted ladder move running on '%s' "
                           "(the script's own PREQEW wait) [v0.130.0]", lineName);
            }
        } else if (!LadderPreqewStillRunning(nowMs)) {
            s_ladderScriptCue   = false;
            s_ladderCueLineAddr = 0;
            Log::Field("FieldNavigation: [LADDER] scripted ladder move finished -- the "
                       "script stopped waiting [v0.130.0]");
        }
    }

    const bool climbing = LadderIsClimbing(mode);
    if (climbing != s_ladderClimbingPrev) {
        if (climbing) {
            LadderClimbBegin(nowMs);
            Log::Field("FieldNavigation: [LADDER] climb started -- %s on ent 0x%08X, "
                       "cadence %u ms (%s) [v0.128.0]",
                       LadderModeIsClimb(mode) ? "engine movement mode"
                                               : "scripted move, no movement mode",
                       block, s_ladderIntervalMs,
                       s_ladderLearned ? "measured from the game" : "default, not yet measured");
        } else {
            Log::Field("FieldNavigation: [LADDER] climb ended, %d mod steps, engine %s "
                       "[v0.128.0]", s_ladderStepIndex,
                       s_ladderEngineEver ? "sounded it itself" : "was silent");
        }
        s_ladderClimbingPrev = climbing;
    }

    if (LadderStepPoll((int)block, mode, nowMs)) {
        Log::Field("FieldNavigation: [LADDER] step -- the game's own ladder sound "
                   "0x%02X, played directly (mode %d, %u ms cadence) [v0.131.0]",
                   LADDER_SOUND_ID, mode, s_ladderIntervalMs);
    }
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

        // v0.18.3.227: Capture talkability for the catalog (race-free).
        // The talk radius the handler just wrote lives at offset 0x1F8
        // (empirically confirmed: TALKRADIUS changed @0x1F8 128->300). Map the
        // entity pointer back to its "others" index and record the radius so
        // RefreshCatalog can treat this entity as talkable regardless of the
        // transient talkonoff flag. Only entities that execute TALKRADIUS reach
        // here, so a nonzero table entry is a definitive "talkable" signal.
        if (FF8Addresses::pFieldStateOthers) {
            uint8_t* obase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (obase) {
                ptrdiff_t d = ent - obase;
                if (d >= 0 && (d % (ptrdiff_t)ENTITY_STRIDE) == 0) {
                    int idx = (int)(d / (ptrdiff_t)ENTITY_STRIDE);
                    if (idx >= 0 && idx < MAX_ENTITIES) {
                        uint16_t r = *(uint16_t*)(ent + 0x1F8);
                        if (r > 0) s_entTalkRadius[idx] = r;
                    }
                }
            }
        }

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

