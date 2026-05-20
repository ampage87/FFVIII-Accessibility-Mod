// battle_tts_ewm_bp_diag.inl — Hardware BP diagnostic (DR0 VEH), target diag, function entry scan.
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// v0.17.8.0: GF_BP_AUTOARM_DIAG gate. The auto-arm path that arms a hardware
// write/read breakpoint on the GF display timer when timer<=3, then captures
// up to GF_BP_MAX_HITS VEH events with full register + GF-state dumps, was a
// v0.10.x investigation tool used to find the GF fire dispatch function entry.
// That investigation closed when the function entry was identified and hooked
// in v0.10.91. The auto-arm path is now leftover diagnostic that floods the
// battle log with 350+ [GF-BP] lines per GF cast in a fraction of a second,
// flagged in Aaron's 2026-05-18 Fire Cavern playthrough.
//
// Set GF_BP_AUTOARM_DIAG to 1 to re-enable the auto-arm path for future
// hardware-BP-based investigation. The VEH handler, GF_BP_ArmAllThreads,
// GF_ScanForFunctionEntry, and the manual-arm paths (if any) remain compiled
// in either way so they're available without removing/restoring code.
#define GF_BP_AUTOARM_DIAG 0
//
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

static void GF_BP_PollKey(void)
{
    // v0.11.01: F12 diagnostic moved to dinput8.cpp for world map.
    // This function is now a no-op in battle. F12 key reserved for diagnostics.
    return;
}

// v0.10.63: Function entry scan — read code bytes backward from the write instruction
// to find the GF timer function's entry point for MinHook.
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
