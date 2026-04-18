// battle_tts_ewm.inl — Enhanced Wait Mode, GF fire prevention, ATB hook, FFNx hook
// Included from battle_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

static void __cdecl HookedGFTimerUpdate(void)
{
    InterlockedIncrement(&s_gfHookCallCount);
    
    if (s_ewmCapGF) {
        InterlockedIncrement(&s_gfHookSkipCount);
        // Skip the function entirely while capped.
        // GF state clamping is handled by EWM_ClampGFState() in the mod thread.
        return;
    }
    
    // Not capped — state68 clamp is released by the ATB hook.
    // Do NOT restore state68 here — let the function set it naturally
    // when the timer reaches 0.
    
    InterlockedIncrement(&s_gfHookPassCount);
    s_originalGFTimerUpdate();
}

// Called from EWM_UpdateBattle() on the mod thread every frame while capped.
// v0.10.91: CONFIRMED fire path is in vanilla engine at 0x004B0400-0x004B0640.
// Three-layer prevention:
//   1. Code patch: 0x004B04B4 MOV→RET prevents state machine case-5 handler
//   2. State68 clamp: prevents state68==5 from being seen by other systems
//   3. Timer function skip: HookedGFTimerUpdate skips display countdown
static void EWM_ClampGFState(void)
{
    // Layer 1: Code patch — prevent state machine from executing fire handler
    // This is the PRIMARY prevention. Patching the opcode at 0x004B04B4 from
    // C7 (MOV [state68],5) to C3 (RET) makes the state machine handler return
    // immediately before any fire setup code runs.
    if (s_gfFirePatchReady && !s_gfFirePatched) {
        __try {
            *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_SAFE_VALUE;  // C7 → C3 (MOV → RET)
            s_gfFirePatched = true;
            Log::Battle("BattleTTS: [GF-PATCH] APPLIED: 0x%08X = 0x%02X (RET) — fire blocked",
                       GF_FIRE_PATCH_ADDR, (unsigned)GF_SAFE_VALUE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Layer 2: Clamp state68 to prevent battle loop from seeing "GF ready"
    __try {
        uint8_t state = *(uint8_t*)GF_STATE68_ADDR;
        if (state == GF_STATE_FIRE) {
            // v0.12.48: A GF has finished loading and is ready to fire.
            // Clear the animation-fired flag for this slot so HP shows GF info.
            int8_t gfSlot = -1;
            __try { gfSlot = *(int8_t*)GF_SLOT_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (gfSlot >= 0 && gfSlot < BATTLE_ALLY_SLOTS) {
                if (s_gfAnimFired[gfSlot]) {
                    s_gfAnimFired[gfSlot] = false;
                    Log::Battle("BattleTTS: [GF-EFFECT] Cleared animFired for slot %d (new GF loading)",
                               (int)gfSlot);
                }
            }
            if (!s_gfState68Clamped) {
                s_gfSavedState68 = state;
                s_gfState68Clamped = true;
            }
            *(uint8_t*)GF_STATE68_ADDR = GF_STATE_SAFE;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Called when EWM cap releases — restore the fire path so GF fires naturally.
static void EWM_RestoreGFPatch(void)
{
    // Restore code patch: C3 → C7 (RET → MOV) — re-enable fire handler
    if (s_gfFirePatched && s_gfFirePatchReady) {
        __try {
            *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;  // C3 → C7
            s_gfFirePatched = false;
            Log::Battle("BattleTTS: [GF-PATCH] RESTORED: 0x%08X = 0x%02X (MOV) — fire enabled",
                       GF_FIRE_PATCH_ADDR, (unsigned)GF_FIRE_VALUE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Release state68 clamp
    s_gfState68Clamped = false;
    s_gfSavedState68 = 0xFF;
}

// Called from Update() to periodically log GF hook stats
static void GF_LogHookStats(void)
{
    DWORD now = GetTickCount();
    if (now - s_gfHookLastLogTick < 1000) return;  // every 1 second
    s_gfHookLastLogTick = now;
    
    LONG calls = InterlockedExchange(&s_gfHookCallCount, 0);
    LONG skips = InterlockedExchange(&s_gfHookSkipCount, 0);
    LONG pass  = InterlockedExchange(&s_gfHookPassCount, 0);
    
    // Read GF-related state bytes
    uint8_t timerVal = 0, gfState = 0xFF, gfActive = 0xFF;
    __try { timerVal = *(uint8_t*)0x01D769D6; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { gfState  = *(uint8_t*)0x01D76868; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // state machine var (jump table dispatch)
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // GF active flag (checked at 0x004B0514)
    
    if (calls > 0 || timerVal > 0 || gfState != 0 || gfActive != 0 || s_gfFirePatched || s_gfScanValid) {
        uint16_t csLoad = 0, csMax = 0;
        int8_t gfSlot = -1;
        __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        // v0.10.81: Also check s_gfFlagHidden — if we've hidden the active flag,
        // gfActive reads as 0 but we still want to log the real timer values.
        // v0.10.88: Removed BATTLE_REAL_ENTITY_STRIDE references (was wrong 0x1D0)
        // Read compStats for display gauge diagnostic instead
        bool gfDiagActive = (gfActive != 0) || s_gfFirePatched;
        if (gfDiagActive && gfSlot >= 0 && gfSlot < BATTLE_ALLY_SLOTS) {
            uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gfSlot * BATTLE_COMP_STATS_STRIDE);
            __try { csLoad = *(uint16_t*)(cs + 0x14); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { csMax = *(uint16_t*)(cs + 0x16); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        
        // v0.10.85: Also read actual byte at patch address for verification
        uint8_t patchByte = 0;
        __try { patchByte = *(uint8_t*)GF_FIRE_PATCH_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        Log::Battle("BattleTTS: [GF-HOOK] calls=%ld skip=%ld pass=%ld timer=%u st68=%u act71=%u capGF=%d patch=%d(0x%02X) cs=%u/%u scan=%d",
                   calls, skips, pass, (unsigned)timerVal, (unsigned)gfState, (unsigned)gfActive, (int)s_ewmCapGF, (int)s_gfFirePatched, (unsigned)patchByte,
                   (unsigned)csLoad, (unsigned)csMax, s_gfScanLogCount);
    }
}

// v0.10.65: GF state snapshot diagnostic — watches the GF state machine region
// to find what triggers the actual GF fire (since timer freeze doesn't prevent it).
// Polls every 200ms and logs any changes in the GF state struct area.
static uint8_t s_gfStateSnap[128] = {};  // snapshot of 0x01D76860-0x01D768DF (state machine area)
static uint8_t s_gfStructSnap[128] = {}; // snapshot of 0x01D76960-0x01D769DF (GF struct area)
static bool s_gfSnapValid = false;
static DWORD s_gfSnapLastTick = 0;

static void GF_PollStateChanges(void)
{
    DWORD now = GetTickCount();
    if (now - s_gfSnapLastTick < 200) return;
    s_gfSnapLastTick = now;
    
    uint8_t newState[128], newStruct[128];
    __try { memcpy(newState, (uint8_t*)0x01D76860, 128); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { memcpy(newStruct, (uint8_t*)0x01D76960, 128); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    if (!s_gfSnapValid) {
        memcpy(s_gfStateSnap, newState, 128);
        memcpy(s_gfStructSnap, newStruct, 128);
        s_gfSnapValid = true;
        return;
    }
    
    // Log changes in state machine region (0x01D76860-0x01D768DF)
    for (int i = 0; i < 128; i++) {
        if (newState[i] != s_gfStateSnap[i]) {
            uint32_t addr = 0x01D76860 + i;
            // Filter out known noisy addresses
            if (addr == 0x01D76862 || addr == 0x01D76870 || addr == 0x01D7686E) continue;  // animation counters
            Log::Battle("BattleTTS: [GF-STATE] 0x%08X: %u -> %u", addr,
                       (unsigned)s_gfStateSnap[i], (unsigned)newState[i]);
        }
    }
    
    // Log changes in GF struct region (0x01D76960-0x01D769DF)
    for (int i = 0; i < 128; i++) {
        if (newStruct[i] != s_gfStructSnap[i]) {
            uint32_t addr = 0x01D76960 + i;
            Log::Battle("BattleTTS: [GF-STRUCT] 0x%08X: %u -> %u", addr,
                       (unsigned)s_gfStructSnap[i], (unsigned)newStruct[i]);
        }
    }
    
    memcpy(s_gfStateSnap, newState, 128);
    memcpy(s_gfStructSnap, newStruct, 128);
}

static void EWM_InstallGFHook()
{
    if (s_gfTimerHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)GF_TIMER_FUNC_ADDR,
        (LPVOID)HookedGFTimerUpdate,
        (LPVOID*)&s_originalGFTimerUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)GF_TIMER_FUNC_ADDR);
    }
    s_gfTimerHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [EWM] GF timer hook @ 0x%08X — %s (trampoline=0x%08X)",
               GF_TIMER_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalGFTimerUpdate);
}

// ============================================================================
// v0.12.48: Battle effect dispatcher hook — detect GF animation fire
// ============================================================================
// battle_read_effect_sub_50AF20 is the engine's battle effect dispatcher.
// It reads battle_magic_id and calls the corresponding effect function from
// func_off_battle_effects_C81774. We hook it to detect when a GF animation
// fires, so we can stop showing GF HP for that slot.
//
// Address: 0x50AF20 (from FFNx naming convention, confirmed via ff8_data.cpp)
// battle_magic_id address: resolved at runtime from *(uint32_t*)(0x50AF20 + 0x3E)

static const uint32_t BATTLE_EFFECT_FUNC_ADDR = 0x50AF20;

typedef void (__cdecl *BattleEffectFn)(void);
static BattleEffectFn s_originalBattleEffect = nullptr;
static bool s_battleEffectHookInstalled = false;
static uint32_t s_battleMagicIdAddr = 0;  // resolved at hook install time

// Known GF effect IDs from FFNx ff8/battle/effects.h
static bool IsGFEffectId(int effectId)
{
    switch (effectId) {
        case 5:    // Leviathan
        case 89:   // Tonberry
        case 94:   // Siren
        case 95:   // Minimog
        case 96:   // BokoChocofire
        case 97:   // BokoChocoflare
        case 98:   // BokoChocometeor
        case 99:   // BokoChocobocle
        case 115:  // Quezacotl
        case 139:  // Phoenix
        case 184:  // Shiva
        case 186:  // Odin
        case 190:  // Doomtrain
        case 198:  // Cactuar
        case 200:  // Ifrit
        case 201:  // Bahamut
        case 202:  // Cerberus
        case 203:  // Alexander
        case 204:  // Brothers
        case 205:  // Eden
        case 277:  // Carbuncle
        case 290:  // Pandemona
        case 324:  // Diablos
        case 325:  // GilgameshZantetsukenReverse
        case 326:  // GilgameshZantetsuken
        case 327:  // GilgameshMasamune
        case 328:  // GilgameshExcaliber
        case 329:  // GilgameshExcalipoor
        case 337:  // Moomba
            return true;
        default:
            return false;
    }
}

// v0.12.49: Poll-based detection instead of hook (hook crashed due to unknown calling convention).
// Polls battle_magic_id every frame. When it changes to a GF effect ID, set s_gfAnimFired.
static int s_prevBattleMagicId = -1;

// v0.12.49: Map GF effect ID to savemap GF index (0-15)
static int GFEffectIdToIndex(int effectId)
{
    switch (effectId) {
        case 115: return 0;   // Quezacotl
        case 184: return 1;   // Shiva
        case 200: return 2;   // Ifrit
        case 94:  return 3;   // Siren
        case 204: return 4;   // Brothers
        case 324: return 5;   // Diablos
        case 277: return 6;   // Carbuncle
        case 5:   return 7;   // Leviathan
        case 290: return 8;   // Pandemona
        case 202: return 9;   // Cerberus
        case 203: return 10;  // Alexander
        case 190: return 11;  // Doomtrain
        case 201: return 12;  // Bahamut
        case 198: return 13;  // Cactuar
        case 89:  return 14;  // Tonberry
        case 205: return 15;  // Eden
        default:  return -1;
    }
}

// Find which party slot (0-2) has a specific GF junctioned.
// Returns -1 if not found.
static int FindPartySlotForGF(int gfIdx)
{
    if (gfIdx < 0 || gfIdx > 15) return -1;
    __try {
        for (int slot = 0; slot < BATTLE_ALLY_SLOTS; slot++) {
            uint8_t charIdx = *(uint8_t*)(0x1CFE74C + slot);  // SAVEMAP_PARTY_FORMATION
            if (charIdx >= 8) continue;
            uint8_t* charBase = (uint8_t*)(0x1CFE0E8 + charIdx * 0x98);  // SAVEMAP_CHAR_DATA_BASE + stride
            uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
            if (gfMask & (1 << gfIdx)) return slot;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static void PollBattleMagicId(void)
{
    if (s_battleMagicIdAddr == 0) return;
    __try {
        int magicId = *(int*)s_battleMagicIdAddr;
        if (magicId != s_prevBattleMagicId) {
            if (IsGFEffectId(magicId)) {
                int gfIdx = GFEffectIdToIndex(magicId);
                int slot = FindPartySlotForGF(gfIdx);
                if (slot >= 0 && slot < BATTLE_ALLY_SLOTS) {
                    s_gfAnimFired[slot] = true;
                    Log::Battle("BattleTTS: [GF-EFFECT] Animation detected: effectId=%d gfIdx=%d slot=%d",
                               magicId, gfIdx, slot);
                }
            }
            s_prevBattleMagicId = magicId;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void EWM_InstallBattleEffectHook()
{
if (s_battleEffectHookInstalled) return;
__try {
    s_battleMagicIdAddr = *(uint32_t*)(BATTLE_EFFECT_FUNC_ADDR + 0x3E);
    s_battleEffectHookInstalled = true;
    s_prevBattleMagicId = -1;
Log::Battle("BattleTTS: [GF-EFFECT] Resolved battle_magic_id at 0x%08X (poll mode)",
           s_battleMagicIdAddr);
} __except(EXCEPTION_EXECUTE_HANDLER) {
Log::Battle("BattleTTS: [GF-EFFECT] EXCEPTION resolving battle_magic_id");
}
}

// ============================================================================
// v0.10.63/70: Hardware write breakpoint diagnostic for GF fire trigger hunt
// ============================================================================
// v0.10.63: Originally targeted 0x01D769D6 (GF loading timer) — found function 0x004B0500.
// v0.10.70: RETARGETED to 0x01D769D0 — bytes that transition to 0 when GF actually fires.
//           This address is written by an UNKNOWN function (not 0x004B0500).
//           Goal: find the instruction that writes to 0x01D769D0, then its function entry.
//
// Sets a hardware write BP using x86 debug registers (DR0).
// When the CPU writes to that address, EXCEPTION_SINGLE_STEP fires and our
// Vectored Exception Handler (VEH) captures the EIP (instruction pointer).
// EIP points to the instruction AFTER the write. We log it and can then
// scan backward to find the function entry for MinHook.
//
// Arm: F12 key during battle while GF is loading.
// Auto-disables after 20 captures to avoid flooding.

static volatile bool s_gfBPArmed = false;       // true while hardware BP is active
static volatile bool s_gfBPWantArm = false;     // set by mod thread, armed by game thread hook
static volatile int  s_gfBPHitCount = 0;        // number of VEH captures so far
static const int     GF_BP_MAX_HITS = 50;  // v0.10.91: increased from 20 to catch fire dispatch amid timer noise
static PVOID         s_gfVEHHandle = nullptr;    // VEH registration handle
static bool          s_gfBPF12WasDown = false;   // edge detection
static DWORD s_accessibilityTID = 0;             // v0.10.91: our thread ID, skip in BP arming

// VEH handler: catches EXCEPTION_SINGLE_STEP from our DR0 hardware breakpoint.
// v0.10.89: Handles READ/WRITE BPs on the GF effect table.
// Runs on whatever thread triggered the access (should be the game thread).
static LONG CALLBACK GF_BP_VectoredHandler(PEXCEPTION_POINTERS pExInfo)
{
    if (pExInfo->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    
    // Check DR6 to confirm this is our DR0 breakpoint (bit 0)
    DWORD dr6 = (DWORD)pExInfo->ContextRecord->Dr6;
    if (!(dr6 & 0x01))
        return EXCEPTION_CONTINUE_SEARCH;  // not our BP
    
    // Clear DR6 bit 0 to acknowledge
    pExInfo->ContextRecord->Dr6 &= ~0x0F;
    
    // v0.10.91: Skip captures from our accessibility thread (self-capture noise)
    if (s_accessibilityTID != 0 && GetCurrentThreadId() == s_accessibilityTID) {
        pExInfo->ContextRecord->Dr6 &= ~0x0F;
        return EXCEPTION_CONTINUE_EXECUTION;  // silently skip, don't count
    }
    
    // v0.10.71: After max captures, silently disarm on whatever thread fires
    if (s_gfBPHitCount >= GF_BP_MAX_HITS) {
        pExInfo->ContextRecord->Dr0 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x03 | (0x0F << 16));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    
    {
        s_gfBPHitCount++;
        DWORD eip = (DWORD)pExInfo->ContextRecord->Eip;
        DWORD dr0 = (DWORD)pExInfo->ContextRecord->Dr0;
        DWORD tid = GetCurrentThreadId();
        
        // Read code bytes around EIP for disassembly context
        // The triggering instruction is BEFORE eip (EIP points to next instruction)
        uint8_t codeBuf[32] = {};
        DWORD codeStart = (eip >= 16) ? eip - 16 : 0;
        __try {
            memcpy(codeBuf, (uint8_t*)codeStart, 32);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // v0.10.89: Capture ALL general-purpose registers.
        // For READ BPs, the read value is typically in one of these registers.
        DWORD regEax = (DWORD)pExInfo->ContextRecord->Eax;
        DWORD regEbx = (DWORD)pExInfo->ContextRecord->Ebx;
        DWORD regEcx = (DWORD)pExInfo->ContextRecord->Ecx;
        DWORD regEdx = (DWORD)pExInfo->ContextRecord->Edx;
        DWORD regEsi = (DWORD)pExInfo->ContextRecord->Esi;
        DWORD regEdi = (DWORD)pExInfo->ContextRecord->Edi;
        DWORD regEbp = (DWORD)pExInfo->ContextRecord->Ebp;
        DWORD regEsp = (DWORD)pExInfo->ContextRecord->Esp;
        
        // Read the value at the BP address (e.g. the function pointer in the effect table)
        uint32_t bpValue = 0;
        __try { bpValue = *(uint32_t*)dr0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Read GF loading context
        uint16_t gfLoadCur = 0, gfLoadMax = 0;
        uint8_t timerVal = 0, state68 = 0, gfActive = 0;
        int8_t gfSlot = -1;
        __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (gfSlot >= 0 && gfSlot < 3) {
            uint8_t* cs = (uint8_t*)(0x01CFF000 + gfSlot * 0x1D0);
            __try { gfLoadCur = *(uint16_t*)(cs + 0x14); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { gfLoadMax = *(uint16_t*)(cs + 0x16); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        __try { timerVal = *(uint8_t*)0x01D769D6; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { state68 = *(uint8_t*)0x01D76868; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Format code hex — 32 bytes centered on EIP
        char hexBuf[128] = {};
        int p = 0;
        for (int i = 0; i < 32; i++)
            p += snprintf(hexBuf + p, sizeof(hexBuf) - p, "%02X ", codeBuf[i]);
        
        Log::Battle("BattleTTS: [GF-BP] #%d ACCESS 0x%08X! TID=%u EIP=0x%08X val@BP=0x%08X",
                   s_gfBPHitCount, dr0, tid, eip, bpValue);
        Log::Battle("BattleTTS: [GF-BP]   regs: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X",
                   regEax, regEbx, regEcx, regEdx);
        Log::Battle("BattleTTS: [GF-BP]   regs: ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X",
                   regEsi, regEdi, regEbp, regEsp);
        Log::Battle("BattleTTS: [GF-BP]   GF: load=%u/%u timer=%u st68=%u act=%u slot=%d",
                   (unsigned)gfLoadCur, (unsigned)gfLoadMax,
                   (unsigned)timerVal, (unsigned)state68, (unsigned)gfActive, (int)gfSlot);
        Log::Battle("BattleTTS: [GF-BP]   code[@0x%08X]: %s", codeStart, hexBuf);
        
        // v0.10.89: Capture call stack (return addresses from stack)
        // Walk ESP upward looking for addresses in executable range
        __try {
            uint32_t* stack = (uint32_t*)regEsp;
            char stackBuf[512] = {};
            int sp = 0;
            sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp, "stack:");
            int found = 0;
            for (int si = 0; si < 32 && found < 8; si++) {
                uint32_t val = stack[si];
                // Check if it looks like a code address (0x004xxxxx or 0x6Exxxxxx for FFNx)
                if ((val >= 0x00401000 && val < 0x00800000) ||
                    (val >= 0x60000000 && val < 0x80000000)) {
                    sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp,
                                   " [ESP+%02X]=0x%08X", si * 4, val);
                    found++;
                }
            }
            if (found > 0)
                Log::Battle("BattleTTS: [GF-BP]   %s", stackBuf);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // After enough captures, disable the BP
        if (s_gfBPHitCount >= GF_BP_MAX_HITS) {
            pExInfo->ContextRecord->Dr0 = 0;
            pExInfo->ContextRecord->Dr7 &= ~(0x03 | (0x0F << 16));
            s_gfBPArmed = false;
            Log::Battle("BattleTTS: [GF-BP] Max captures reached, BP disabled.");
        }
    }
    
    return EXCEPTION_CONTINUE_EXECUTION;
}

// v0.10.71/76/89: Arm hardware BP on ALL threads in the process.
// Enumerates threads via ToolHelp32, suspends each, sets DR0, resumes.
// v0.10.76: Takes target address as parameter.
// v0.10.89: Takes condition and length bits for DR7.
//   condition: 0x01 = write-only, 0x03 = read/write
//   length:    0x00 = 1 byte, 0x01 = 2 bytes, 0x03 = 4 bytes
static void GF_BP_ArmAllThreads(uint32_t targetAddr, uint8_t condition = 0x01, uint8_t length = 0x00)
{
    if (s_gfBPArmed) return;
    
    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Log::Battle("BattleTTS: [GF-BP] CreateToolhelp32Snapshot FAILED (err=%u)", GetLastError());
        return;
    }
    
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    int armed = 0, failed = 0;
    
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            
            HANDLE hThread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                FALSE, te.th32ThreadID);
            if (!hThread) { failed++; continue; }
            
            bool isSelf = (te.th32ThreadID == myTid);
            // v0.10.91: Skip our accessibility thread to avoid self-capture
            bool isOurThread = (s_accessibilityTID != 0 && te.th32ThreadID == s_accessibilityTID);
            if (isOurThread) { CloseHandle(hThread); continue; }
            if (!isSelf) SuspendThread(hThread);
            
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(hThread, &ctx)) {
                ctx.Dr0 = targetAddr;
                ctx.Dr7 &= ~(0x03 | (0x0F << 16));  // clear DR0 enable + condition + length
                ctx.Dr7 |= 0x01;                      // local enable DR0
                ctx.Dr7 |= ((DWORD)(condition & 0x03) << 16);  // condition bits
                ctx.Dr7 |= ((DWORD)(length & 0x03) << 18);     // length bits
                
                if (SetThreadContext(hThread, &ctx)) {
                    armed++;
                } else {
                    failed++;
                }
            } else {
                failed++;
            }
            
            if (!isSelf) ResumeThread(hThread);
            CloseHandle(hThread);
        } while (Thread32Next(snap, &te));
    }
    
    CloseHandle(snap);
    s_gfBPArmed = true;
    s_gfBPHitCount = 0;
    const char* condStr = (condition == 0x03) ? "READ/WRITE" : (condition == 0x01) ? "WRITE" : "EXEC";
    int lenBytes = (length == 0x03) ? 4 : (length == 0x01) ? 2 : 1;
    Log::Battle("BattleTTS: [GF-BP] Hardware %s BP armed on 0x%08X (%d-byte) — ALL THREADS (%d armed, %d failed)",
               condStr, targetAddr, lenBytes, armed, failed);
}

// v0.10.91: Auto-arm READ BP on display timer 0x01D769D6 when GF loading
// starts. compStats+0x14/+0x16 are ALWAYS ZERO (confirmed v0.10.90) — they
// are NOT the GF loading counter. The real countdown is the display timer
// at 0x01D769D6 (counts ~48→0). Something reads this value and triggers
// the fire when it hits 0. A hardware READ BP will catch that instruction.
//
// CRITICAL FIX: Skip arming on our own accessibility thread. In v0.10.90,
// ALL VEH captures were from our thread (TID that polls memory), producing
// only FFNx SEH handler EIPs. By excluding our thread, the BP only fires
// on the game thread where the actual fire dispatch code runs.
static uint8_t s_gfAutoArmLastActive = 0;  // previous value of 0x01D76971
static bool s_gfAutoArmDone = false;        // only auto-arm once per battle

static void GF_BP_AutoArm(void)
{
    if (!s_inBattle || s_gfBPArmed || s_gfAutoArmDone) return;
    
    // Record our TID on first call so we can skip it when arming
    if (s_accessibilityTID == 0) s_accessibilityTID = GetCurrentThreadId();
    
    uint8_t gfActive = 0;
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    // v0.10.91 fix: Arm as soon as we see gfActive==1, not just on 0→1 edge.
    // The GF may already be active when we first poll (command issued before
    // our thread starts checking). s_gfAutoArmDone ensures we only arm once.
    s_gfAutoArmLastActive = gfActive;
    
    if (gfActive == 1) {
        // v0.10.91: Target the display timer at 0x01D769D6 (uint16).
        // CRITICAL: Don't arm immediately! The timer is read/written ~60x/sec by
        // the hooked timer function. If we arm now, we'll burn 20 captures in <1sec,
        // all from the timer decrement code, and never see the fire dispatch.
        // Instead, wait until timer <= 3 so we catch the fire dispatch read when
        // the timer hits 0, not all the decrement reads during loading.
        uint32_t targetAddr = 0x01D769D6;
        uint16_t timerVal = 0;
        __try { timerVal = *(uint16_t*)targetAddr; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        if (timerVal == 0 || timerVal > 3) {
            // timerVal==0: timer hasn't started yet (goes 0→47→0)
            // timerVal>3: still counting down, wait until closer to fire
            return;
        }
        
        // Timer is 1, 2, or 3 — about to fire, arm now!
        Log::Battle("BattleTTS: [GF-BP] AUTO-ARM: timer at %u, arming READ/WRITE BP on 0x%08X",
                   (unsigned)timerVal, targetAddr);
        Log::Battle("BattleTTS: [GF-BP] AUTO-ARM: skipping our TID=%u to avoid self-capture",
                   (unsigned)s_accessibilityTID);
        GF_BP_ArmAllThreads(targetAddr, 0x03, 0x01);  // read/write, 2-byte
        s_gfAutoArmDone = true;
        ScreenReader::Speak("Breakpoint armed on display timer", true);
    }
}

// ============================================================================
// v0.10.96: Target selection diagnostic (F12 key)
// ============================================================================
// Takes 2 snapshots of the battle menu state region (0x01D76800-0x01D76C00,
// 1024 bytes) while the player moves the target cursor between enemies/allies.
// F12 press cycle:
//   Stage 0 → 1: Snapshot with cursor on target A
//   Stage 1 → 2: Move cursor to target B, press F12 → diff + log
//   Stage 2 + F12: Reset for another round
//
// Focuses on cursor-like byte changes: small values (<32), small deltas (1-6).
// Also logs menuPhase, activeChar, cmdCursor to identify the targeting phase.

static const uint32_t TGTDIAG_SCAN_BASE = 0x01D76800;
static const int TGTDIAG_SCAN_SIZE = 1024;  // 0x01D76800-0x01D76BFF

struct TargetDiagSnapshot {
    uint8_t region[1024];
    uint8_t menuPhase;      // 0x01D768D0
    uint8_t activeChar;     // battle_current_active_character_id
    uint8_t cmdCursor;      // 0x01D76843
    uint8_t subCursor;      // 0x01D76844 (known sub-menu cursor)
};

static TargetDiagSnapshot s_tgtDiagSnaps[2] = {};
static int s_tgtDiagStage = 0;  // 0=ready, 1=first snap, 2=diffed

static void TgtDiag_TakeSnapshot(int idx)
{
    TargetDiagSnapshot& snap = s_tgtDiagSnaps[idx];
    memset(&snap, 0, sizeof(snap));
    __try { memcpy(snap.region, (uint8_t*)TGTDIAG_SCAN_BASE, TGTDIAG_SCAN_SIZE); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { snap.menuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (s_pActiveCharId) { __try { snap.activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    __try { snap.cmdCursor = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { snap.subCursor = *(uint8_t*)0x01D76844; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.11.01: F12 diagnostic gutted (was Draw diagnostic v0.10.107-112).
// F12 now handled in dinput8.cpp for world map diagnostics.
// Draw spell constants retained for runtime use.
static const uint32_t DRAW_SPELL_BASE = 0x1D28F18;   // Enemy 1 slot 0
static const int      DRAW_SLOTS_PER_ENEMY = 4;
static const int      DRAW_SLOT_SIZE = 4;             // bytes per slot
static const int      DRAW_ENEMY_STRIDE = 0x47;       // bytes between enemy 1 and enemy 2
static const int      DRAW_ENEMY_COUNT = 4;            // enemies 1-4 (slots 3-6)

static void GF_BP_PollKey(void)
{
    // v0.11.01: F12 diagnostic moved to dinput8.cpp for world map.
    // This function is now a no-op in battle. F12 key reserved for diagnostics.
    return;
}

// v0.10.63: Function entry scan — read code bytes backward from the write instruction
// to find the GF timer function's entry point for MinHook.
static bool s_gfFuncScanDone = false;

static void GF_ScanForFunctionEntry(void)
{
    if (s_gfFuncScanDone) return;
    if (s_gfBPHitCount < 1) return;  // need at least one BP hit first
    s_gfFuncScanDone = true;
    
    // The write instruction is at 0x004B063B (EIP after = 0x004B063F).
    // Scan backward looking for function boundaries:
    //   - CC (INT3 padding)
    //   - C3 (RET) or C2 xx xx (RET imm16) followed by our code
    //   - 90 (NOP padding)
    //   - 55 8B EC (PUSH EBP; MOV EBP, ESP — standard prologue)
    //   - 83 EC xx (SUB ESP, xx — FPO prologue without PUSH EBP)
    
    const uint32_t writeAddr = 0x004B063B;
    const uint32_t scanStart = writeAddr - 0x200;  // scan 512 bytes back
    
    Log::Battle("BattleTTS: [GF-FUNC] === Function entry scan from write @ 0x%08X ===", writeAddr);
    
    // Dump code in 32-byte chunks from scanStart to writeAddr+16
    __try {
        for (uint32_t addr = scanStart; addr <= writeAddr + 16; addr += 32) {
            uint8_t* p = (uint8_t*)addr;
            char hex[200] = {};
            int pos = 0;
            for (int i = 0; i < 32 && addr + i <= writeAddr + 16; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", p[i]);
            }
            Log::Battle("BattleTTS: [GF-FUNC] 0x%08X: %s", addr, hex);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [GF-FUNC] EXCEPTION reading code");
    }
    
    // Also scan for specific patterns
    __try {
        uint8_t* base = (uint8_t*)scanStart;
        int scanLen = (int)(writeAddr - scanStart);
        
        for (int i = scanLen - 1; i >= 0; i--) {
            uint32_t addr = scanStart + i;
            uint8_t b = base[i];
            
            // Look for RET (C3) or INT3 (CC) — potential function boundary
            if (b == 0xC3 || b == 0xCC) {
                // The next byte after RET/INT3 could be the function entry
                uint32_t candidate = addr + 1;
                uint8_t next1 = base[i + 1];
                uint8_t next2 = (i + 2 < scanLen) ? base[i + 2] : 0;
                uint8_t next3 = (i + 3 < scanLen) ? base[i + 3] : 0;
                
                Log::Battle("BattleTTS: [GF-FUNC] Boundary @ 0x%08X: %02X | next: %02X %02X %02X (candidate entry: 0x%08X)",
                           addr, b, next1, next2, next3, candidate);
            }
            
            // Look for PUSH EBP (55) followed by MOV EBP,ESP (8B EC)
            if (b == 0x55 && i + 2 < scanLen && base[i+1] == 0x8B && base[i+2] == 0xEC) {
                Log::Battle("BattleTTS: [GF-FUNC] PROLOGUE (push ebp; mov ebp,esp) @ 0x%08X",
                           addr);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [GF-FUNC] EXCEPTION in pattern scan");
    }
    
    Log::Battle("BattleTTS: [GF-FUNC] === End scan ===");
}

static void __cdecl HookedATBUpdate(void)
{
    // v0.10.88: Initialize GF timer scan snapshot when GF loading starts
    if (!s_gfScanValid) {
        uint8_t gfAct = 0;
        __try { gfAct = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (gfAct == 1) {
            memcpy(s_gfScanSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            s_gfScanValid = true;
            Log::Battle("BattleTTS: [GF-SCAN] Snapshot initialized (GF active)");
        }
    }
    
    // v0.10.85: RET code patch (belt-and-suspenders, kept alongside sticky hide).
    if (s_gfFirePatchReady) {
        if (s_ewmCapGF && !s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            if (*p == GF_FIRE_VALUE) {
                *p = GF_SAFE_VALUE;
                s_gfFirePatched = true;
            }
        } else if (!s_ewmCapGF && s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            *p = GF_FIRE_VALUE;
            s_gfFirePatched = false;
        }
    }
    
    // v0.10.87: Sticky/sandwich gfActive hide REMOVED (v0.10.88).
    // All flag-hiding approaches break ATB or menus. Need to find the
    // GF timer decrement function instead.
    
    // v0.10.69: Clamp GF state68 on the GAME THREAD (before battle loop reads it).
    // The mod thread clamp was too late — race condition with the game loop.
    if (s_ewmCapGF) {
        uint8_t st = *(uint8_t*)GF_STATE68_ADDR;
        if (st == GF_STATE_FIRE) {
            if (!s_gfState68Clamped) {
                s_gfSavedState68 = st;
                s_gfState68Clamped = true;
            }
            *(uint8_t*)GF_STATE68_ADDR = GF_STATE_SAFE;
        }
    } else if (s_gfState68Clamped) {
        // DON'T restore state68 to 5 (fire) — let the GF timer function
        // set it naturally when the timer reaches 0 after uncapping.
        // Just clear our tracking flag.
        s_gfState68Clamped = false;
    }
    
    // v0.10.79-82: Real GF timer pre-cap REMOVED (v0.10.88).
    // 0x01D27D94 was Enemy 1's ATB, not a GF timer. Stride is 0xD0, not 0x1D0.
    
    if (!s_ewmShouldCap) {
        s_originalATBUpdate();
        
        // v0.10.88: Per-frame GF timer scan on early-return path
        if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
            uint8_t newSnap[GF_SCAN_BYTES];
            memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
                int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
                int16_t newVal = *(int16_t*)(newSnap + i);
                int16_t delta = newVal - oldVal;
                // Look for decrementing values (delta -1 to -4) with values in timer range
                if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                    Log::Battle("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                               i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                    s_gfScanLogCount++;
                }
            }
            memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
        }
        
        return;
    }
    
    uint8_t excludeSlot = s_ewmCapExcludeSlot;
    
    // --- PRE-CAP: save real ATB values, set to 0 ---
    uint32_t savedATB[BATTLE_TOTAL_SLOTS] = {};
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        } else {
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        }
    }
    
    // v0.10.95: PRE-CAP for GF loading counter — per-slot via entity+0x7C.
    // The ATB function also increments compStats[slot]+0x14 (GF loading gauge).
    // Same sandwich: save → zero → call → measure → restore+cap.
    // Now iterates all ally slots instead of just gfSlot.
    uint16_t savedGFLoad[BATTLE_ALLY_SLOTS] = {};
    uint16_t gfLoadMax[BATTLE_ALLY_SLOTS] = {};
    bool gfLoadActive[BATTLE_ALLY_SLOTS] = {};
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
                savedGFLoad[gs] = *pGFLoad;
                gfLoadMax[gs] = *(uint16_t*)(cs + 0x16);
                *pGFLoad = 0;  // zero so original function increments from 0
                gfLoadActive[gs] = true;
            }
        }
    }
    
    // --- CALL ORIGINAL: ATB increments from 0, status timers run normally ---
    s_originalATBUpdate();
    
    // v0.13.57: POST-FREEZE for GF loading counter — restore to exact
    // pre-sandwich value (matching ATB freeze semantics). GF loading
    // only advances when freeze is released (i.e., during the active
    // player's GF-cast animation), never during menu/freeze windows.
    for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
        if (!gfLoadActive[gs]) continue;
        uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
        uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
        *pGFLoad = savedGFLoad[gs];
    }
    
    // v0.10.82: Real GF timer post-cap REMOVED (v0.10.88) — was Enemy 1's ATB.
    
    // v0.13.57: POST-FREEZE — restore ATB to exact pre-sandwich value.
    // Previously (v0.13.56 and earlier) this was a "cap at max-1" sandwich
    // that ADDED the per-frame increment on top of savedATB, then clamped.
    // That preserved race order for entities below max-1 but CONVERGED
    // everyone at max-1 during long freezes (GF summons, damage windows),
    // erasing the natural ATB race — multiple entities would tie at 11999
    // and all dispatch simultaneously when the freeze released.
    //
    // Freeze semantics match Aaron's turn-based retrofit model: "the enemy's
    // ATB and other party members ATB are held in place" — held literally
    // means their value does not change. When the freeze releases, each
    // entity resumes from exactly where it was; whoever was closest to max
    // wins the natural race a few frames later (no ties created by the
    // mod).
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            *pCurATB = (uint16_t)savedATB[slot];
        } else {
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            *pCurATB = (uint32_t)savedATB[slot];
        }
    }

    // v0.10.95: Per-slot GF max inflation on the GAME THREAD.
    // Uses entity+0x7C per-character flag instead of global gfSlot.
    // Iterates all ally slots: any slot with entity+0x7C != 0 gets its
    // compStats+0x16 inflated to 0xFFFF to prevent the fire check from passing.
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs2 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax = (uint16_t*)(cs2 + 0x16);
                uint16_t curMax = *pMax;
                if (curMax != 0xFFFF && curMax > 0) {
                    if (!s_gfMaxInflated[gs]) {
                        s_gfRealMax[gs] = curMax;
                        s_gfMaxInflated[gs] = true;
                    }
                    *pMax = 0xFFFF;
                }
            }
        }
    } else {
        // Cap released — restore real max for all inflated slots
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            if (s_gfMaxInflated[gs]) {
                uint8_t* cs3 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax2 = (uint16_t*)(cs3 + 0x16);
                if (*pMax2 == 0xFFFF && s_gfRealMax[gs] > 0) {
                    *pMax2 = s_gfRealMax[gs];
                }
                s_gfMaxInflated[gs] = false;
                s_gfRealMax[gs] = 0;
            }
        }
    }
    
    // v0.10.87: Sticky gfActive zero REMOVED (v0.10.88).
    
    // v0.10.88: Per-frame GF timer scan on main sandwich path
    if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
        uint8_t newSnap[GF_SCAN_BYTES];
        memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
        for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
            int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
            int16_t newVal = *(int16_t*)(newSnap + i);
            int16_t delta = newVal - oldVal;
            if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                Log::Battle("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                           i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                s_gfScanLogCount++;
            }
        }
        memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
    }
}

static bool s_ewmEnabled = true;          // Enhanced Wait Mode toggle
static bool s_ewmFreezing = false;        // currently requesting freeze
static bool s_ewmConfigLoaded = false;    // config file has been read
static bool s_ewmOKeyWasDown = false;     // edge detection for O key

// v0.13.51: EWM toggle persistence moved to the shared Config INI.
// Legacy ewm_config.txt (single "1"/"0" byte) is imported by Config::Load on
// first run and deleted, so existing installs preserve their setting.
static void EWM_LoadConfig()
{
    if (s_ewmConfigLoaded) return;
    s_ewmConfigLoaded = true;
    Config::Load();
    s_ewmEnabled = (Config::GetInt("ewm_enabled", 1) != 0);
    Log::Battle("BattleTTS: [EWM] Config loaded: ewm_enabled=%d (from %s)",
               (int)s_ewmEnabled, Config::GetPath());
}

static void EWM_SaveConfig()
{
    Config::SetInt("ewm_enabled", s_ewmEnabled ? 1 : 0);
}

static void EWM_PollToggle()
{
    bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool oPressed = oDown && !s_ewmOKeyWasDown;
    s_ewmOKeyWasDown = oDown;
    if (!oPressed) return;
    s_ewmEnabled = !s_ewmEnabled;
    EWM_SaveConfig();
    // If disabling, immediately release cap
    if (!s_ewmEnabled) {
        s_ewmShouldCap = false;
        s_ewmFreezing = false;
        s_ewmCapExcludeSlot = 0xFF;
        s_ewmCapGF = false;
    }
    const char* msg = s_ewmEnabled ? "Enhanced Wait Mode on" : "Enhanced Wait Mode off";
    ScreenReader::Speak(msg, true);
    Log::Battle("BattleTTS: [EWM] Toggled: %s", msg);
}

// Install the MinHook on the ATB update function.
// Called once from Initialize().
static void EWM_InstallHook()
{
    if (s_ewmHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR,
        (LPVOID)HookedATBUpdate,
        (LPVOID*)&s_originalATBUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR);
    }
    s_ewmHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [EWM] ATB hook @ 0x%08X — %s (trampoline=0x%08X)",
               ATB_UPDATE_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalATBUpdate);
}

// ============================================================================
// v0.13.55: Turn/action dispatch hook (sub_483470)
// ============================================================================
// sub_483470 at 0x00483470 is the engine's "process ready characters /
// dispatch turns" function. Called every frame from the main battle loop at
// 0x0047D7F1, it iterates entities and dispatches any whose ATB is at max
// (queued earlier by sub_483EB0). Dispatching means:
//   - Player character → set activeChar to 0-2, open command menu
//   - Enemy → start their attack animation
//
// Previous versions of this fix layered an ATB cap + grace + cooldown to
// prevent actions from firing during damage/TTS windows. That approach has
// an unavoidable race: the mod thread polls every ~16 ms, but the game
// thread runs the ATB hook + main battle loop every frame. When the mod
// thread releases the cap, in the SAME game-thread tick the ATB function
// can let an enemy top out, sub_483EB0 queues them, sub_483470 dispatches
// them, and the attack begins — all before the mod thread polls again.
// [0x01D27B00] (engine action-in-progress) is not a persistent "animation
// playing" flag for fast actions; sub_47E080(5) only sets it briefly
// inside sub_483EB0 during queuing.
//
// This hook is the clean intervention point. Returning early from
// sub_483470 means no dispatch can happen, period — no enemy attack starts,
// no command menu opens. The mod thread's decision takes effect on the
// very next call to sub_483470 (next game frame), closing the race.
//
// Block conditions (OR'd):
//   - damageOrActionActive — our existing signal bundle (damage TTS,
//     HP pending, anim flags, grace period, post-action cooldown)
//   - activeChar < 3 — player's command menu is open; nothing else should
//     dispatch until they confirm (also stops queued enemy actions that
//     slipped in from firing mid-decision)
static const uint32_t PROCESS_READY_FUNC_ADDR = 0x00483470;
typedef void (__cdecl *ProcessReadyFn)(void);
static ProcessReadyFn s_originalProcessReady = nullptr;
static bool s_processReadyHookInstalled = false;
static volatile bool s_blockProcessReady = false;
static volatile LONG s_processReadyCalls = 0;
static volatile LONG s_processReadyBlocks = 0;
static volatile LONG s_processReadyPasses = 0;
static DWORD s_processReadyLogTick = 0;

// v0.13.56: Second dispatch-layer hook on sub_482F80. In the main battle
// loop, sub_483470 and sub_482F80 are called back-to-back under the same
// engine gate ([0x01D27B00]==0 && running-flag==1). sub_483470 appears to
// handle player-ATB-ready dispatch (opens command menus); sub_482F80
// appears to handle the execution side (starts enemy animations, damage
// impact, etc.). v0.13.55 blocked 6/6 sub_483470 calls during the bug
// window yet the enemy attack still landed, which means sub_482F80 is
// the path for enemy action execution. Hook both to close the gap.
static const uint32_t ACTION_EXECUTE_FUNC_ADDR = 0x00482F80;
typedef void (__cdecl *ActionExecuteFn)(void);
static ActionExecuteFn s_originalActionExecute = nullptr;
static bool s_actionExecuteHookInstalled = false;
static volatile LONG s_actionExecuteCalls  = 0;
static volatile LONG s_actionExecuteBlocks = 0;
static volatile LONG s_actionExecutePasses = 0;

static void __cdecl HookedProcessReady(void)
{
    InterlockedIncrement(&s_processReadyCalls);
    if (s_blockProcessReady) {
        InterlockedIncrement(&s_processReadyBlocks);
        return;
    }
    InterlockedIncrement(&s_processReadyPasses);
    if (s_originalProcessReady) {
        s_originalProcessReady();
    }
}

static void __cdecl HookedActionExecute(void)
{
    InterlockedIncrement(&s_actionExecuteCalls);
    if (s_blockProcessReady) {
        InterlockedIncrement(&s_actionExecuteBlocks);
        return;
    }
    InterlockedIncrement(&s_actionExecutePasses);
    if (s_originalActionExecute) {
        s_originalActionExecute();
    }
}

static void EWM_InstallProcessReadyHook()
{
    if (s_processReadyHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)PROCESS_READY_FUNC_ADDR,
        (LPVOID)HookedProcessReady,
        (LPVOID*)&s_originalProcessReady);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)PROCESS_READY_FUNC_ADDR);
    }
    s_processReadyHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [DISPATCH] sub_483470 hook @ 0x%08X — %s (trampoline=0x%08X)",
               PROCESS_READY_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalProcessReady);
}

static void EWM_InstallActionExecuteHook()
{
    if (s_actionExecuteHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)ACTION_EXECUTE_FUNC_ADDR,
        (LPVOID)HookedActionExecute,
        (LPVOID*)&s_originalActionExecute);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)ACTION_EXECUTE_FUNC_ADDR);
    }
    s_actionExecuteHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [DISPATCH] sub_482F80 hook @ 0x%08X — %s (trampoline=0x%08X)",
               ACTION_EXECUTE_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalActionExecute);
}

// Periodically log dispatch hook stats for BOTH hooks so we can distinguish
// calls/blocks/passes and see which path actions are taking. Logs every
// second whenever there's any activity at all (calls or block-flag).
static void EWM_LogDispatchStats()
{
    DWORD now = GetTickCount();
    if (now - s_processReadyLogTick < 1000) return;
    s_processReadyLogTick = now;
    LONG prCalls  = InterlockedExchange(&s_processReadyCalls, 0);
    LONG prBlocks = InterlockedExchange(&s_processReadyBlocks, 0);
    LONG prPasses = InterlockedExchange(&s_processReadyPasses, 0);
    LONG aeCalls  = InterlockedExchange(&s_actionExecuteCalls, 0);
    LONG aeBlocks = InterlockedExchange(&s_actionExecuteBlocks, 0);
    LONG aePasses = InterlockedExchange(&s_actionExecutePasses, 0);
    if (prCalls > 0 || aeCalls > 0 || s_blockProcessReady) {
        Log::Battle("BattleTTS: [DISPATCH] sub_483470: calls=%ld blocks=%ld passes=%ld | sub_482F80: calls=%ld blocks=%ld passes=%ld | block-flag=%d",
                   prCalls, prBlocks, prPasses,
                   aeCalls, aeBlocks, aePasses,
                   (int)s_blockProcessReady);
    }
}

// ============================================================================
// v0.10.77: FFNx GF loading counter hook
// ============================================================================
// FFNx (not the vanilla engine) writes to compStats[slot]+0x14 (master GF loading
// counter). Confirmed v0.10.76 via hardware write BP: all writes come from FFNx
// DLL space. Our ATB hook sandwich on +0x14 had no effect because FFNx overwrites
// the value on a separate code path.
//
// Strategy: find FFNx's module at runtime via the JMP at set_midi_volume (0x0046BB40),
// scan for the signature B9 16 F0 CF 01 66 89 06, walk backward to find the
// function entry, and MinHook it with the same cap-at-max-1 sandwich.
// ============================================================================

// s_ffnxGFHookInstalled defined earlier (forward declaration near GF timer hook section)
typedef void (__cdecl *FFNxBattleUpdateFn)(void);
static FFNxBattleUpdateFn s_originalFFNxBattleUpdate = nullptr;
static uint32_t s_ffnxGFFuncAddr = 0;  // resolved address of FFNx function

// s_ffnxHookCallCount forward-declared earlier near GF timer hook section

static void __cdecl HookedFFNxBattleUpdate(void)
{
    InterlockedIncrement(&s_ffnxHookCallCount);

    // If not capping GF, or no GF is loading, just call through
    if (!s_ewmCapGF) {
        s_originalFFNxBattleUpdate();
        return;
    }

    // Check if a GF is actively loading
    uint8_t gfActive = 0;
    int8_t gfSlot = -1;
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (gfActive != 1 || gfSlot < 0 || gfSlot >= BATTLE_ALLY_SLOTS) {
        // No GF loading — call through unmodified
        s_originalFFNxBattleUpdate();
        return;
    }

    // Sandwich: save compStats[gfSlot]+0x14, call original, restore+cap at max-1
    uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gfSlot * BATTLE_COMP_STATS_STRIDE);
    uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
    uint16_t savedLoad = *pGFLoad;
    uint16_t gfMax = *(uint16_t*)(cs + 0x16);

    s_originalFFNxBattleUpdate();

    // After the call, FFNx may have incremented +0x14.
    // Compute the new value and cap at max-1.
    uint16_t newLoad = *pGFLoad;
    if (gfMax > 1 && newLoad >= gfMax) {
        *pGFLoad = gfMax - 1;  // cap: prevent GF from firing
    }
}

// Find FFNx module base by following the E9 JMP at set_midi_volume (0x0046BB40).
// Returns 0 on failure.
static uint32_t FindFFNxModuleBase(void)
{
    __try {
        uint8_t* pSetMidi = (uint8_t*)0x0046BB40;
        if (*pSetMidi != 0xE9) {
            Log::Battle("BattleTTS: [FFNx-GF] set_midi_volume @0x0046BB40 is not a JMP (byte=0x%02X)",
                       (unsigned)*pSetMidi);
            return 0;
        }
        // E9 rel32: target = addr + 5 + *(int32_t*)(addr+1)
        int32_t rel = *(int32_t*)(pSetMidi + 1);
        uint32_t target = 0x0046BB40 + 5 + rel;
        Log::Battle("BattleTTS: [FFNx-GF] set_midi_volume JMP target = 0x%08X", target);

        // Use VirtualQuery to find the allocation base (= module base)
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == 0) {
            Log::Battle("BattleTTS: [FFNx-GF] VirtualQuery failed for 0x%08X", target);
            return 0;
        }
        uint32_t moduleBase = (uint32_t)(uintptr_t)mbi.AllocationBase;
        Log::Battle("BattleTTS: [FFNx-GF] FFNx module base = 0x%08X", moduleBase);
        return moduleBase;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [FFNx-GF] EXCEPTION resolving FFNx module base");
        return 0;
    }
}

// Scan a single module for the GF loading writer signature.
// Signature: B9 16 F0 CF 01 66 89 06 = MOV ECX,0x01CFF016; MOV [ESI],AX
// Returns the address of the first byte of the match, or 0.
static uint32_t ScanModuleForSignature(uint32_t moduleBase)
{
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)moduleBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        uint32_t moduleSize = nt->OptionalHeader.SizeOfImage;

        static const uint8_t sig[] = { 0xB9, 0x16, 0xF0, 0xCF, 0x01, 0x66, 0x89, 0x06 };
        static const int sigLen = sizeof(sig);

        uint8_t* base = (uint8_t*)moduleBase;
        for (uint32_t i = 0; i + sigLen <= moduleSize; i++) {
            bool match = true;
            for (int j = 0; j < sigLen; j++) {
                if (base[i + j] != sig[j]) { match = false; break; }
            }
            if (match) {
                uint32_t addr = moduleBase + i;
                Log::Battle("BattleTTS: [FFNx-GF] Signature found at 0x%08X in module 0x%08X (size=0x%X)",
                           addr, moduleBase, moduleSize);
                return addr;
            }
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Scan ALL loaded modules in the process for the signature.
// The GF loading writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
static uint32_t ScanAllModulesForSignature(void)
{
    HANDLE hProc = GetCurrentProcess();
    HMODULE modules[512];
    DWORD cbNeeded = 0;
    if (!EnumProcessModules(hProc, modules, sizeof(modules), &cbNeeded)) {
        Log::Battle("BattleTTS: [FFNx-GF] EnumProcessModules failed (err=%u)", GetLastError());
        return 0;
    }
    int count = cbNeeded / sizeof(HMODULE);
    Log::Battle("BattleTTS: [FFNx-GF] Scanning %d loaded modules for signature...", count);

    for (int i = 0; i < count; i++) {
        uint32_t base = (uint32_t)(uintptr_t)modules[i];
        uint32_t result = ScanModuleForSignature(base);
        if (result != 0) return result;
    }
    Log::Battle("BattleTTS: [FFNx-GF] Signature not found in any loaded module");
    return 0;
}

// Walk backward from sigAddr to find the function entry point.
// Looks for CC/90 inter-function padding (MSVC pattern).
static uint32_t FindFunctionEntry(uint32_t sigAddr)
{
    __try {
        uint8_t* p = (uint8_t*)sigAddr;
        // Scan backward up to 0x400 bytes
        for (int i = 1; i < 0x400; i++) {
            uint8_t b = p[-i];
            if (b == 0xCC || b == 0x90) {
                // Found padding — the function entry is the first non-padding byte after this
                // Continue backward through the padding
                int padStart = i;
                while (padStart < 0x400 && (p[-padStart] == 0xCC || p[-padStart] == 0x90))
                    padStart++;
                // Now p[-padStart] is non-padding (end of previous function).
                // The entry is at p[-(padStart-1)] = first padding byte... no.
                // Actually: p[-i] is the first padding byte we found (closest to sig).
                // Walk backward through padding. The function entry is the byte AFTER
                // the last padding byte (closest to our code).
                uint32_t entry = sigAddr - i + 1;
                // But we need to continue backward past ALL padding
                int j = i;
                while (j < 0x400) {
                    uint8_t prev = p[-j];
                    if (prev != 0xCC && prev != 0x90) break;
                    j++;
                }
                entry = sigAddr - j + 1;
                Log::Battle("BattleTTS: [FFNx-GF] Function entry at 0x%08X (sig-0x%X, padding at sig-0x%X)",
                           entry, (sigAddr - entry), i);
                return entry;
            }
            // Also check for RET (C3) which ends the previous function
            if (b == 0xC3) {
                uint32_t entry = sigAddr - i + 1;
                Log::Battle("BattleTTS: [FFNx-GF] Function entry at 0x%08X (after RET at sig-0x%X)",
                           entry, i);
                return entry;
            }
        }
        Log::Battle("BattleTTS: [FFNx-GF] Could not find function entry (no padding/RET in 0x400 bytes)");
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [FFNx-GF] EXCEPTION scanning for function entry");
        return 0;
    }
}

static void EWM_InstallFFNxGFHook(void)
{
    if (s_ffnxGFHookInstalled) return;

    // Step 1+2: Scan all loaded modules for signature
    // The writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
    uint32_t sigAddr = ScanAllModulesForSignature();
    if (sigAddr == 0) {
        Log::Battle("BattleTTS: [FFNx-GF] Signature scan failed — GF timer hook skipped");
        return;
    }

    // Step 3: Find function entry
    uint32_t funcAddr = FindFunctionEntry(sigAddr);
    if (funcAddr == 0) {
        Log::Battle("BattleTTS: [FFNx-GF] Function entry not found — GF timer hook skipped");
        return;
    }
    s_ffnxGFFuncAddr = funcAddr;

    // Dump first 32 bytes of the function for diagnostic
    __try {
        uint8_t* code = (uint8_t*)funcAddr;
        char hex[200] = {};
        int p = 0;
        for (int b = 0; b < 32; b++)
            p += snprintf(hex + p, sizeof(hex) - p, "%02X ", code[b]);
        Log::Battle("BattleTTS: [FFNx-GF] Function code[0..31]: %s", hex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Step 4: MinHook it
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)funcAddr,
        (LPVOID)HookedFFNxBattleUpdate,
        (LPVOID*)&s_originalFFNxBattleUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)funcAddr);
    }
    s_ffnxGFHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [FFNx-GF] MinHook @ 0x%08X — %s (trampoline=0x%08X)",
               funcAddr, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalFFNxBattleUpdate);
}

// Menu phase lifecycle for a typical command:
//   0 or 32 = command menu / sub-menu open (DECIDING)
//   64 = Limit Break showing (DECIDING)
//   3 = brief transition after command select (DECIDING — loading target UI)
//   11 = target selection (DECIDING)
//   14 = target confirmed, action committed (EXECUTING — release here)
//   21, 23, 33, 34 = animation/cleanup (EXECUTING)
//   1, 4 = turn setup transitions (still DECIDING — keep freeze)
//
// BUG FIX (v0.10.38): When active_char_id changes (new turn starts), menuPhase
// may still be 34 from the PREVIOUS character's execution. We must always freeze
// on a new turn edge regardless of menuPhase. Only use menuPhase for RELEASE.

static uint8_t s_ewmLastActiveChar = 0xFF;  // track active_char_id changes for turn edge
static bool s_ewmNewTurnGrace = false;       // v0.10.41: suppress phase-based release until non-executing phase seen

// v0.13.53: Post-turn grace period. When a player's action completes and
// activeChar transitions from 0-2 to 0xFF, hold the excludeSlot=0xFF cap for
// this window so no entity (player OR enemy) can top out and fire in the
// 1-2 frame race window before engine flags catch up. After the grace
// expires the cap releases normally and whoever has the highest ATB gets
// the next turn (engine-decided, as usual).
static const DWORD EWM_POST_TURN_GRACE_MS = 1000;
static uint8_t s_ewmPrevSeenActiveChar = 0xFF;
static DWORD s_ewmPostTurnGraceEnd = 0;

// v0.13.54: Post-action cooldown. The post-turn grace covers player-action
// endings, but there's a second race: when something like a GF summon
// completes, engine's [0x01D27B00] transitions 1→0, the mod thread releases
// the cap, and in the SAME game-frame the ATB hook (uncapped) lets an enemy
// sitting at 11999 top out, sub_483EB0 queues their action, and the engine
// executes it before the mod thread polls again. We bridge that gap by
// tracking the last time any damage/action signal was seen active and
// holding the cap for this cooldown afterward.
static const DWORD EWM_POST_ACTION_COOLDOWN_MS = 500;
static DWORD s_ewmLastSignalTime = 0;

// v0.13.57: Damage-anim transition diagnostic. Polls [0x01D280C0] (the
// engine's damage animation counter) every mod frame. When it transitions
// 0→1 (first damage animation starts), log a full state snapshot: all 8
// entity ATB values, s_ewmShouldCap, activeChar, [0x01D27B00], and ms
// elapsed since the last freeze-release. This tells us EXACTLY what state
// the game was in at the instant a damage animation started — which is
// the real "bug moment" we've been trying to localize.
//
// Also logs s_ewmShouldCap transitions and post-release ATB values for
// 500ms to see whether any entity slipped past the freeze.
static uint8_t s_diagPrevDamageAnim = 0;
static uint32_t s_diagPrevActionInProgress = 0;
static bool s_diagPrevShouldCap = false;
static DWORD s_diagFreezeReleaseTime = 0;
static int s_diagPostReleaseLogsRemaining = 0;

// v0.13.58: Per-slot turn counter for comparing EWM-on vs EWM-off turn
// ratios. Detects a turn start by watching each slot's ATB transition from
// high (>10000) to low (<2000) — the engine resets an acting entity's
// ATB to 0 at the start of its action. This detection is independent of
// EWM state (works with or without the freeze sandwich), so the counts
// are directly comparable across EWM-on/off battles against the same
// enemy type. Counters reset on each battle start; a summary is logged
// on battle end.
static uint32_t s_prevSlotATB[BATTLE_TOTAL_SLOTS] = {};
static int      s_slotTurnCount[BATTLE_TOTAL_SLOTS] = {};
static bool     s_slotATBInit = false;
static bool     s_turnCountPrevInBattle = false;

static bool EWM_IsExecutingPhase(uint8_t phase)
{
    return (phase == 14 || phase == 21 || phase == 23 || phase == 33 || phase == 34);
}

// v0.13.57: Build a compact ATB snapshot string for diagnostic logging.
// Format: "s0=cur/max s1=cur/max ... s3=cur/max(HP=x)"
static void EWM_FormatATBSnapshot(char* buf, int bufSize)
{
    int pos = 0;
    pos += snprintf(buf + pos, bufSize - pos, "ATB=[");
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS && pos < bufSize - 32; slot++) {
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                uint16_t cur = *(uint16_t*)(base + BENT_CUR_ATB);
                uint16_t max = *(uint16_t*)(base + BENT_MAX_ATB);
                if (max > 0) {
                    pos += snprintf(buf + pos, bufSize - pos, "s%d=%u/%u ",
                                   slot, (unsigned)cur, (unsigned)max);
                }
            } else {
                uint32_t cur = *(uint32_t*)(base + BENT_CUR_ATB);
                uint32_t max = *(uint32_t*)(base + BENT_MAX_ATB);
                uint32_t hp  = *(uint32_t*)(base + BENT_CUR_HP);
                if (max > 0) {
                    pos += snprintf(buf + pos, bufSize - pos, "s%d=%u/%u(hp%u) ",
                                   slot, cur, max, hp);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    pos += snprintf(buf + pos, bufSize - pos, "]");
}

// v0.13.57: Poll engine flags and ATB state every frame. Log on transitions
// and during the 500 ms window after freeze release.
static void EWM_PollDiagnostics(uint8_t activeChar)
{
    uint8_t  curDamageAnim = 0;
    uint32_t curActionInProgress = 0;
    __try { curDamageAnim = *(uint8_t*)0x01D280C0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { curActionInProgress = *(uint32_t*)0x01D27B00; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    DWORD now = GetTickCount();

    // -- [0x01D280C0] transitions (damage animation counter) --
    if (curDamageAnim != s_diagPrevDamageAnim) {
        char atbBuf[512] = {};
        EWM_FormatATBSnapshot(atbBuf, sizeof(atbBuf));
        DWORD msSinceRelease = s_diagFreezeReleaseTime ? (now - s_diagFreezeReleaseTime) : 0;
        Log::Battle("BattleTTS: [DMG-DIAG] [0x01D280C0] %u->%u | activeChar=%d shouldCap=%d [0x01D27B00]=%u msSinceRelease=%u | %s",
                   (unsigned)s_diagPrevDamageAnim, (unsigned)curDamageAnim,
                   (int)activeChar, (int)s_ewmShouldCap,
                   (unsigned)curActionInProgress,
                   (unsigned)msSinceRelease, atbBuf);
        s_diagPrevDamageAnim = curDamageAnim;
    }

    // -- [0x01D27B00] transitions (engine action-in-progress flag) --
    if (curActionInProgress != s_diagPrevActionInProgress) {
        Log::Battle("BattleTTS: [ACT-DIAG] [0x01D27B00] %u->%u | activeChar=%d shouldCap=%d engAnim=%u",
                   (unsigned)s_diagPrevActionInProgress, (unsigned)curActionInProgress,
                   (int)activeChar, (int)s_ewmShouldCap, (unsigned)curDamageAnim);
        s_diagPrevActionInProgress = curActionInProgress;
    }

    // -- s_ewmShouldCap transitions (freeze state) --
    if (s_ewmShouldCap != s_diagPrevShouldCap) {
        char atbBuf[512] = {};
        EWM_FormatATBSnapshot(atbBuf, sizeof(atbBuf));
        Log::Battle("BattleTTS: [FRZ-DIAG] shouldCap %d->%d | activeChar=%d engAnim=%u [0x01D27B00]=%u excludeSlot=%u | %s",
                   (int)s_diagPrevShouldCap, (int)s_ewmShouldCap,
                   (int)activeChar, (unsigned)curDamageAnim,
                   (unsigned)curActionInProgress, (unsigned)s_ewmCapExcludeSlot,
                   atbBuf);
        if (s_diagPrevShouldCap && !s_ewmShouldCap) {
            // Freeze just released — start the post-release trace window.
            s_diagFreezeReleaseTime = now;
            s_diagPostReleaseLogsRemaining = 30;  // ~500 ms at 60 Hz polling
        }
        s_diagPrevShouldCap = s_ewmShouldCap;
    }

    // -- Post-release trace window: log every frame for up to ~500 ms --
    if (s_diagPostReleaseLogsRemaining > 0 && !s_ewmShouldCap) {
        char atbBuf[512] = {};
        EWM_FormatATBSnapshot(atbBuf, sizeof(atbBuf));
        DWORD ms = now - s_diagFreezeReleaseTime;
        Log::Battle("BattleTTS: [POST-REL] ms=%u activeChar=%d engAnim=%u [0x01D27B00]=%u | %s",
                   (unsigned)ms, (int)activeChar,
                   (unsigned)curDamageAnim, (unsigned)curActionInProgress, atbBuf);
        s_diagPostReleaseLogsRemaining--;
    }
}

// v0.13.59: Per-slot turn counter. Runs from BattleTTS::Update() unconditionally
// (not gated behind EWM_UpdateBattle's early returns) so ratios are directly
// comparable between EWM-on and EWM-off battles. Detects an entity's turn by
// watching for a high→low ATB transition (engine resets acting entity's ATB
// to 0 at action start). Logs each turn and a battle summary on exit.
//
// Reset and summary are driven by OnBattleEnter()/OnBattleExit() via
// EWM_ResetTurnCount() and EWM_LogTurnCountSummary(), which see the s_inBattle
// transitions reliably regardless of EWM state or init-announce progress.

// Reset counters at battle entry. Called from OnBattleEnter().
static void EWM_ResetTurnCount()
{
    for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
        s_slotTurnCount[i] = 0;
        s_prevSlotATB[i] = 0;
    }
    s_slotATBInit = false;
    s_turnCountPrevInBattle = true;
    Log::Battle("BattleTTS: [TURN-COUNT] === Battle START (EWM=%s) — counters reset ===",
               s_ewmEnabled ? "ON" : "OFF");
}

// Log summary at battle exit. Called from OnBattleExit().
static void EWM_LogTurnCountSummary()
{
    if (!s_turnCountPrevInBattle) return;  // nothing to summarize
    int partyTotal = s_slotTurnCount[0] + s_slotTurnCount[1] + s_slotTurnCount[2];
    int enemyTotal = 0;
    for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
        enemyTotal += s_slotTurnCount[i];
    }
    Log::Battle("BattleTTS: [TURN-COUNT] === Battle END (EWM=%s) — summary ===",
               s_ewmEnabled ? "ON" : "OFF");
    Log::Battle("BattleTTS: [TURN-COUNT]   Party:   s0=%d s1=%d s2=%d (total=%d)",
               s_slotTurnCount[0], s_slotTurnCount[1], s_slotTurnCount[2], partyTotal);
    // v0.13.60 bug fix: BATTLE_TOTAL_SLOTS=7, so enemy slots are s3..s6 (4 slots).
    // The old format string included s7 which was an out-of-bounds read.
    Log::Battle("BattleTTS: [TURN-COUNT]   Enemies: s3=%d s4=%d s5=%d s6=%d (total=%d)",
               s_slotTurnCount[3], s_slotTurnCount[4],
               s_slotTurnCount[5], s_slotTurnCount[6], enemyTotal);
    if (enemyTotal > 0) {
        int ratio_x100 = (partyTotal * 100) / enemyTotal;
        Log::Battle("BattleTTS: [TURN-COUNT]   Ratio party:enemy = %d.%02d:1 (%d vs %d)",
                   ratio_x100 / 100, ratio_x100 % 100, partyTotal, enemyTotal);
    } else {
        Log::Battle("BattleTTS: [TURN-COUNT]   Ratio party:enemy = N/A (no enemy turns observed)");
    }
    s_turnCountPrevInBattle = false;
}

// Per-frame poll. Called from BattleTTS::Update() while s_inBattle is true.
// Independent of EWM state — counts are apples-to-apples comparable across
// EWM-on and EWM-off battles.
static void EWM_TrackTurnCount()
{
    if (!s_inBattle) return;  // defensive; the caller already gates on s_inBattle

    // Sample ATB for each slot and detect high→low transitions.
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint32_t curATB = 0;
        uint32_t maxATB = 0;
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                curATB = *(uint16_t*)(base + BENT_CUR_ATB);
                maxATB = *(uint16_t*)(base + BENT_MAX_ATB);
            } else {
                curATB = *(uint32_t*)(base + BENT_CUR_ATB);
                maxATB = *(uint32_t*)(base + BENT_MAX_ATB);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        // Skip empty/inactive slots
        if (maxATB == 0) continue;

        // Detect action start: ATB was high (>10000), now low (<2000)
        if (s_slotATBInit && s_prevSlotATB[slot] > 10000 && curATB < 2000) {
            s_slotTurnCount[slot]++;
            int partyTotal = s_slotTurnCount[0] + s_slotTurnCount[1] + s_slotTurnCount[2];
            int enemyTotal = 0;
            for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
                enemyTotal += s_slotTurnCount[i];
            }
            Log::Battle("BattleTTS: [TURN-COUNT] slot=%d turn#%d (ATB %u→%u) | running: party=%d enemy=%d",
                       slot, s_slotTurnCount[slot],
                       s_prevSlotATB[slot], curATB,
                       partyTotal, enemyTotal);
        }
        s_prevSlotATB[slot] = curATB;
    }
    s_slotATBInit = true;
}


// GF loading diagnostic (v0.10.57): logs ATB values for all slots while cap
// is active. Tells us whether GF charge gauge uses entity+0x0C (same as ATB)
// or a separate counter. Run from mod thread, reads memory directly.
static DWORD s_ewmDiagLastTick = 0;
static int s_ewmDiagCount = 0;
static const int EWM_DIAG_MAX = 40;  // max samples per cap session

static void EWM_DiagLogATB(const char* label)
{
    DWORD now = GetTickCount();
    if (now - s_ewmDiagLastTick < 500) return;  // every 500ms
    if (s_ewmDiagCount >= EWM_DIAG_MAX) return;
    s_ewmDiagLastTick = now;
    s_ewmDiagCount++;
    
    char buf[512];
    int pos = 0;
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                uint16_t cur = *(uint16_t*)(base + BENT_CUR_ATB);
                uint16_t max = *(uint16_t*)(base + BENT_MAX_ATB);
                uint16_t hp  = *(uint16_t*)(base + BENT_CUR_HP);
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                               "s%d=%u/%u(hp%u) ", slot, (unsigned)cur, (unsigned)max, (unsigned)hp);
            } else {
                uint32_t cur = *(uint32_t*)(base + BENT_CUR_ATB);
                uint32_t max = *(uint32_t*)(base + BENT_MAX_ATB);
                uint32_t hp  = *(uint32_t*)(base + BENT_CUR_HP);
                if (max > 0) {  // only log active enemies
                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                   "s%d=%u/%u(hp%u) ", slot, cur, max, hp);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    Log::Battle("BattleTTS: [EWM-DIAG] #%d %s ATB: %s", s_ewmDiagCount, label, buf);
}

// Called every frame during battle from Update() (mod thread).
// Sets s_ewmShouldCap + s_ewmCapExcludeSlot which the game-thread hook reads.
//
// ATB CAPPING (v0.10.55): Instead of freezing ATB entirely, we let ATB fill
// at normal Speed-based rates but cap all entities at ATB_max - 1. This means:
//   - Nobody can trigger a new turn while the player is deciding
//   - Speed-based turn ratios are fully preserved (no extra player turns)
//   - When the cap lifts, the fastest entity triggers next (within 1 tick)
// The deciding character is excluded from capping (their ATB is already at max).
static void EWM_UpdateBattle()
{
    if (!s_ewmEnabled || !s_ewmHookInstalled) {
        if (s_ewmFreezing) {
            s_ewmShouldCap = false;
            s_ewmCapExcludeSlot = 0xFF;
            s_ewmCapGF = false;
            s_ewmFreezing = false;
            Log::Battle("BattleTTS: [EWM] ATB cap released (EWM disabled or hook missing)");
        }
        return;
    }
    
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // v0.13.57: Run diagnostic poll every frame. Logs on transitions and
    // during post-freeze-release windows. Pure observation — no side effects
    // on game state or EWM decisions.
    EWM_PollDiagnostics(activeChar);

    // v0.13.51: Damage TTS hold — release state machine.
    // FlushHPAnnouncements raises s_ewmHoldForDamageTTS when it announces damage.
    // We release when SAPI reports the voice(s) are no longer speaking. To avoid
    // releasing before SAPI has started (its queue latency after Speak()), we
    // require s_ewmDamageTTSStarted to be set first (meaning we observed
    // SPRS_IS_SPEAKING at least once). Two escape hatches: max-hold timeout
    // and start-never-observed timeout (for super-short messages or speak
    // failures).
    if (s_ewmHoldForDamageTTS) {
        DWORD elapsed = GetTickCount() - s_ewmDamageTTSStartTick;
        bool speaking = ScreenReader::IsSpeaking();
        if (speaking) s_ewmDamageTTSStarted = true;

        if (elapsed >= EWM_DAMAGE_TTS_MAX_MS) {
            s_ewmHoldForDamageTTS = false;
            Log::Battle("BattleTTS: [EWM] Damage TTS hold released (max timeout %u ms)",
                       (unsigned)elapsed);
        } else if (s_ewmDamageTTSStarted && !speaking) {
            s_ewmHoldForDamageTTS = false;
            Log::Battle("BattleTTS: [EWM] Damage TTS hold released (speech done after %u ms)",
                       (unsigned)elapsed);
        } else if (!s_ewmDamageTTSStarted && elapsed >= EWM_DAMAGE_TTS_START_TIMEOUT_MS) {
            s_ewmHoldForDamageTTS = false;
            Log::Battle("BattleTTS: [EWM] Damage TTS hold released (speech never observed after %u ms)",
                       (unsigned)elapsed);
        }
    }

    // v0.13.52 / v0.13.53 / v0.13.54: Cap during any damage/action window,
    // during a post-turn grace period, AND for a brief cooldown after the
    // last signal clears. Without the cooldown, there's a game-thread frame
    // race at the MOMENT the cap releases: mod thread sees all signals clear
    // and drops the cap, but in the same game-thread tick an enemy sitting
    // at 11999 tops out, sub_483EB0 queues their action, the engine executes
    // it, and by the time the mod thread polls again the damage has already
    // landed. The cooldown extends cap-engaged by EWM_POST_ACTION_COOLDOWN_MS
    // after the last anyActiveNow=true frame, giving us several poll cycles
    // to observe any newly-queued action and re-engage before execution.
    //
    // Signals checked:
    //   s_ewmHoldForDamageTTS — our SAPI damage speech hold flag
    //   s_anyHpPending        — HP deltas detected, awaiting flush
    //   s_damageAnimWasActive — our tracking of the damage animation flag
    //   [0x01D280C0] != 0     — engine's damage animation flag
    //   [0x01D27B00] != 0     — engine's "action in progress" flag
    //   post-turn grace       — 1s window after a player action ends
    //   post-action cooldown  — 500ms window after the last signal cleared
    if (s_ewmPrevSeenActiveChar < 3 && activeChar == 0xFF) {
        // Player's turn just ended — open the grace window.
        s_ewmPostTurnGraceEnd = GetTickCount() + EWM_POST_TURN_GRACE_MS;
        Log::Battle("BattleTTS: [EWM] Post-turn grace started (%ums) prevChar=%u",
                   (unsigned)EWM_POST_TURN_GRACE_MS, (unsigned)s_ewmPrevSeenActiveChar);
    }
    s_ewmPrevSeenActiveChar = activeChar;
    bool postTurnGraceActive = (GetTickCount() < s_ewmPostTurnGraceEnd);

    uint8_t  engineDamageAnim = 0;
    uint32_t engineActionInProgress = 0;
    __try { engineDamageAnim = *(uint8_t*)0x01D280C0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { engineActionInProgress = *(uint32_t*)0x01D27B00; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    bool anyActiveNow = s_ewmHoldForDamageTTS || s_anyHpPending ||
                        s_damageAnimWasActive ||
                        (engineDamageAnim != 0) ||
                        (engineActionInProgress != 0);

    // v0.13.54: Track last-active time for the post-action cooldown.
    DWORD nowTick = GetTickCount();
    if (anyActiveNow) {
        s_ewmLastSignalTime = nowTick;
    }
    bool postActionCooldown = (s_ewmLastSignalTime != 0) &&
                              (nowTick - s_ewmLastSignalTime < EWM_POST_ACTION_COOLDOWN_MS);

    bool damageOrActionActive = postTurnGraceActive || anyActiveNow || postActionCooldown;

    // v0.13.55: Update dispatch-block flag. HookedProcessReady reads this
    // on the game thread and returns early when set, preventing sub_483470
    // from dispatching new turns / enemy actions. Matches the cap conditions
    // plus "player is deciding" — anything the cap was supposed to block
    // at the queuing layer, this blocks at the dispatch layer as a backstop.
    s_blockProcessReady = damageOrActionActive || (activeChar < 3);

    // If any transition-hold signal is active AND nobody is deciding yet,
    // force the cap on EVERY slot (excludeSlot=0xFF). If a player was already
    // deciding (activeChar < 3) the command menu is already visible and the
    // normal cap logic below handles the rest — no need for a full-everyone
    // cap that would demote the active character's ATB.
    if (damageOrActionActive && activeChar == 0xFF) {
        s_ewmCapGF = true;
        EWM_ClampGFState();
        s_ewmCapExcludeSlot = 0xFF;
        s_ewmShouldCap = true;
        if (!s_ewmFreezing) {
            s_ewmFreezing = true;
            Log::Battle("BattleTTS: [EWM] ATB capped during transition (grace=%d cooldown=%d tts=%d hp=%d anim=%d engAnim=%d engAct=%u)",
                       (int)postTurnGraceActive, (int)postActionCooldown,
                       (int)s_ewmHoldForDamageTTS, (int)s_anyHpPending,
                       (int)s_damageAnimWasActive,
                       (int)engineDamageAnim, (unsigned)engineActionInProgress);
        }
        return;  // skip normal logic — hold dominates
    }

    // v0.10.75: GF cap stays active during turn transitions (activeChar==0xFF)
    // as long as a GF is loading. Only release when an action is executing.
    // This closes the gap where the GF loading counter crossed max during
    // the brief uncapped frames between turns.
    // v0.10.88: Removed sticky hide accounting (flag-hiding abandoned)
    bool gfIsLoading = false;
    __try { gfIsLoading = (*(uint8_t*)0x01D76971 == 1); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    if (activeChar < 3) {
        s_ewmCapGF = true;  // player is deciding — cap GF timer
        EWM_ClampGFState();   // v0.10.68: clamp state68 to prevent GF fire
        
        // Detect new turn edge: active_char_id changed to a valid player slot
        bool newTurnEdge = (activeChar != s_ewmLastActiveChar);
        s_ewmLastActiveChar = activeChar;
        
        uint32_t menuPhaseDword = 0;
        __try { menuPhaseDword = *(uint32_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        // v0.13.51 bug fix #2: 0x01D768D0 is dual-purpose — it holds a small
        // phase integer (0-43) on the command menu, but when a submenu opens
        // the engine writes a FUNCTION POINTER here (0x004XXXXX range, i.e.
        // >= 0x00400000). The submenu per-frame handler at 0x4FDD90 actually
        // calls `call dword ptr [0x1d768d0]` every frame, treating the value
        // as a code pointer. Reading a single byte from this address can
        // coincidentally match an executing-phase value (14/21/23/33/34)
        // during submenu navigation, which was releasing the ATB cap and
        // letting enemies attack while the player navigated a submenu.
        // Treat any dword >= 0x00400000 as "submenu open == definitely deciding",
        // matching the same threshold used in battle_tts_menu.inl.
        //
        // v0.13.51 hotfix (post-BAT): The dword check alone is not enough.
        // Draw is a multi-phase submenu where 0x01D768D0 holds PLAIN PHASE
        // INTEGERS (14 = spell list, 21/23 = transitions, 25 = Stock/Cast)
        // during navigation — no function pointer gets written. Phases 14/21/23
        // are in EWM's "executing" list (correct for Attack, wrong for Draw),
        // so the cap was released mid-submenu and enemies got free attacks.
        // Fix: ALSO consult s_inSubmenu (maintained in menu.inl, declared in
        // hp.inl) — it's the authoritative "player is in a submenu" flag,
        // including Draw's phase-transition exit suppression. If it's true,
        // we treat the phase as deciding no matter what the byte says.
        bool submenuOpen = (menuPhaseDword >= 0x00400000) || s_inSubmenu;
        uint8_t menuPhase = (uint8_t)(menuPhaseDword & 0xFF);
        
        if (newTurnEdge) {
            // New turn — ALWAYS cap, regardless of menuPhase.
            // menuPhase may still be 34 from the previous character's execution.
            s_ewmFreezing = true;
            s_ewmCapExcludeSlot = activeChar;
            s_ewmShouldCap = true;
            s_ewmNewTurnGrace = true;  // suppress phase-based release until non-executing phase seen
            s_ewmDiagCount = 0;  // reset diagnostic counter for new cap session
            s_ewmDiagLastTick = 0;
            Log::Battle("BattleTTS: [EWM] ATB capped (new turn, char=%d, phase=%u)",
                       (int)activeChar, (unsigned)menuPhase);
        } else {
            // Same turn continuing — use menuPhase to decide cap vs release.
            // v0.13.51: `submenuOpen` from the dword check forces the "deciding"
            // path even if the low byte of the function pointer looks like an
            // executing phase.
            if (submenuOpen || !EWM_IsExecutingPhase(menuPhase)) {
                // Player is deciding (command menu, sub-menu, target select)
                if (s_ewmNewTurnGrace) {
                    // Grace period satisfied — we've seen a non-executing phase,
                    // meaning the engine has transitioned to the command menu.
                    s_ewmNewTurnGrace = false;
                    Log::Battle("BattleTTS: [EWM] Grace cleared (deciding phase=%u)", (unsigned)menuPhase);
                }
                if (!s_ewmFreezing) {
                    s_ewmFreezing = true;
                    Log::Battle("BattleTTS: [EWM] ATB capped (deciding, char=%d, phase=%u)",
                               (int)activeChar, (unsigned)menuPhase);
                }
                s_ewmCapExcludeSlot = activeChar;
                s_ewmShouldCap = true;
                EWM_DiagLogATB("cap");  // v0.10.57: log ATB values while capped
            } else {
                // Phase says executing — but check grace period first
                if (s_ewmNewTurnGrace) {
                    // Still in grace period: phase=34 is STALE from previous character.
                    // Keep cap active until we see a non-executing phase.
                    s_ewmShouldCap = true;
                } else {
                    // Action executing (no grace) — release cap
                    if (s_ewmFreezing) {
                        s_ewmFreezing = false;
                        s_ewmShouldCap = false;
                        s_ewmCapExcludeSlot = 0xFF;
                        s_ewmCapGF = false;
                        EWM_RestoreGFPatch();
                        Log::Battle("BattleTTS: [EWM] ATB cap released (executing, phase=%u)",
                                   (unsigned)menuPhase);
                    }
                }
            }
        }
    } else {
        s_ewmLastActiveChar = 0xFF;
        s_ewmNewTurnGrace = false;
        // Keep GF cap active during turn transitions if a GF is loading.
        // Only release when no GF is loading.
        if (gfIsLoading) {
            s_ewmCapGF = true;
            EWM_ClampGFState();
        } else {
            if (s_ewmCapGF) {
                s_ewmCapGF = false;
                EWM_RestoreGFPatch();
            }
        }
        if (s_ewmFreezing) {
            s_ewmFreezing = false;
            s_ewmShouldCap = false;
            s_ewmCapExcludeSlot = 0xFF;
            Log::Battle("BattleTTS: [EWM] ATB cap released (no turn active, gfLoading=%d)",
                       (int)gfIsLoading);
        }
    }
}

// ============================================================================
// Turn announcement + Command menu TTS (v0.10.11)
// ============================================================================
