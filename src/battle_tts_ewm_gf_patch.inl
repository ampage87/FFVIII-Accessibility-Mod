// battle_tts_ewm_gf_patch.inl — GF fire prevention (code patch + state68 clamp + timer hook).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// Three-layer prevention strategy (v0.10.91 — see EWM_ClampGFState comments):
//   1. Code patch at 0x004B04B4: MOV→RET prevents state machine case-5 handler
//   2. State68 clamp: prevents state68==5 from being seen by other systems
//   3. Timer function skip: HookedGFTimerUpdate skips display countdown
//
// Diagnostic helpers: GF_LogHookStats periodic counter dump; GF_PollStateChanges
// snapshots the state machine + struct regions every 200ms looking for fire
// triggers.

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
