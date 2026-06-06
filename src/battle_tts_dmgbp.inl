// battle_tts_dmgbp.inl — Hardware write BP on damage display address
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.14.2 — Option 3 of the three-front diagnostic
// ============================================================================
//
// Goal: catch the engine instruction that writes the damage value to
// 0x01D2834A (BATTLE_DAMAGE_DISPLAY_ADDR, uint16). v0.13.86 confirmed this
// address holds both damage AND heal values — the engine writes the
// visible number whenever a damage popup is about to render. Whatever
// instruction does that write is upstream of the visibility decision, and
// from there we can walk forward to find the rendering trigger.
//
// Mechanism: x86 hardware debug register DR1 (DR0 is occupied by the
// existing GF-BP infrastructure in battle_tts_ewm.inl). Vectored exception
// handler catches EXCEPTION_SINGLE_STEP, checks DR6 bit 1, logs full
// context if the hit was ours.
//
// Width: 2-byte BP (uint16). 0x01D2834A is even (74 dec) so 2-byte alignment
// is satisfied. Condition: write-only (DR7 bits 20-21 = 0b01).
//
// Arm timing: at OnBattleEnter. Hit-capped at 50 to bound log volume; after
// the cap fires, the BP self-disarms inside the handler.
//
// Shares s_accessibilityTID from battle_tts_ewm.inl to avoid self-capture
// on our own polling thread (mod-thread reads of 0x01D2834A would otherwise
// trigger the BP repeatedly via FlushHPAnnouncements' display read).
//
// CAVEAT: write-only BPs don't fire on x86 reads, so our mod-thread reads
// of 0x01D2834A are silent. But we DO write nothing to it — we only read.
// So self-capture isn't actually possible on this BP. Skip is defensive
// only, mirroring the GF-BP pattern.

static const uint32_t DMGBP_TARGET_ADDR = 0x01D2834A;  // BATTLE_DAMAGE_DISPLAY_ADDR (uint16)
static const int      DMGBP_MAX_HITS    = 50;

static volatile bool  s_dmgBPArmed     = false;
static volatile LONG  s_dmgBPHitCount  = 0;
static PVOID          s_dmgVEHHandle   = nullptr;

// ============================================================================
// VEH handler
// ============================================================================
//
// Two VEHs are registered (this one + GF-BP's). Windows calls them in
// registration order. Each must check its OWN DR-bit in DR6 and return
// EXCEPTION_CONTINUE_SEARCH if the hit isn't theirs, so the other handler
// gets a chance.
//
// We use DR1, so we check DR6 bit 1.

static LONG CALLBACK Dmg_BP_VectoredHandler(PEXCEPTION_POINTERS pExInfo)
{
    if (pExInfo->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD dr6 = (DWORD)pExInfo->ContextRecord->Dr6;
    if (!(dr6 & 0x02))
        return EXCEPTION_CONTINUE_SEARCH;  // DR1 bit not set — not our hit

    // Acknowledge by clearing the low 4 bits of DR6 (matches GF-BP pattern).
    pExInfo->ContextRecord->Dr6 &= ~0x0F;

    // Skip our own polling thread (defensive — write BP shouldn't fire on
    // our reads, but mirror the GF-BP defence-in-depth).
    if (s_accessibilityTID != 0 && GetCurrentThreadId() == s_accessibilityTID) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    LONG hitCount = InterlockedIncrement(&s_dmgBPHitCount);
    if (hitCount > DMGBP_MAX_HITS) {
        // Disarm DR1 in this thread's context. Other threads still have
        // DR1 armed but they'll hit the same cap and self-disarm. By the
        // time the hit count is way over the cap, all threads will have
        // dropped the BP.
        pExInfo->ContextRecord->Dr1 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x04 | (0x0F << 20));  // L1 + cond + len
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    DWORD eip = (DWORD)pExInfo->ContextRecord->Eip;
    DWORD tid = GetCurrentThreadId();

    // Read the value AT the BP address. On x86, after a write instruction
    // EIP points to the NEXT instruction, and the write has already
    // completed — so reading here gives us the post-write value.
    uint16_t curVal = 0;
    __try { curVal = *(uint16_t*)DMGBP_TARGET_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Capture registers — the source operand of the write is typically in
    // one of EAX/ECX/EDX/etc, depending on the instruction's encoding.
    DWORD regEax = (DWORD)pExInfo->ContextRecord->Eax;
    DWORD regEbx = (DWORD)pExInfo->ContextRecord->Ebx;
    DWORD regEcx = (DWORD)pExInfo->ContextRecord->Ecx;
    DWORD regEdx = (DWORD)pExInfo->ContextRecord->Edx;
    DWORD regEsi = (DWORD)pExInfo->ContextRecord->Esi;
    DWORD regEdi = (DWORD)pExInfo->ContextRecord->Edi;
    DWORD regEbp = (DWORD)pExInfo->ContextRecord->Ebp;
    DWORD regEsp = (DWORD)pExInfo->ContextRecord->Esp;

    // Code bytes around EIP. The triggering instruction precedes EIP; with
    // typical write instructions (5–7 bytes for `mov [imm32], reg/imm`)
    // the instruction starts at EIP-5 to EIP-7. Capture 16 bytes BEFORE
    // and 16 bytes AT/AFTER for context.
    uint8_t codeBuf[32] = {};
    DWORD codeStart = (eip >= 16) ? eip - 16 : 0;
    __try { memcpy(codeBuf, (uint8_t*)codeStart, 32); } __except(EXCEPTION_EXECUTE_HANDLER) {}

    char hexBuf[128] = {};
    int p = 0;
    for (int i = 0; i < 32; i++)
        p += snprintf(hexBuf + p, sizeof(hexBuf) - p, "%02X ", codeBuf[i]);

    Log::Battle("BattleTTS: [DMG-BP] #%ld ACCESS 0x%08X! TID=%u EIP=0x%08X val@addr=%u",
                hitCount, DMGBP_TARGET_ADDR, tid, eip, (unsigned)curVal);
    Log::Battle("BattleTTS: [DMG-BP]   regs: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X",
                regEax, regEbx, regEcx, regEdx);
    Log::Battle("BattleTTS: [DMG-BP]   regs: ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X",
                regEsi, regEdi, regEbp, regEsp);
    Log::Battle("BattleTTS: [DMG-BP]   code[0x%08X..]: %s",
                codeStart, hexBuf);

    // Walk the stack for return addresses in executable space (FF8 main exe
    // 0x00400000–0x00800000, and FFNx DLL space 0x60000000–0x80000000).
    __try {
        uint32_t* stack = (uint32_t*)regEsp;
        char stackBuf[512] = {};
        int sp = 0;
        sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp, "stack:");
        int found = 0;
        for (int si = 0; si < 32 && found < 8; si++) {
            uint32_t val = stack[si];
            if ((val >= 0x00401000 && val < 0x00800000) ||
                (val >= 0x60000000 && val < 0x80000000)) {
                sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp,
                               " [ESP+%02X]=0x%08X", si * 4, val);
                found++;
            }
        }
        if (found > 0)
            Log::Battle("BattleTTS: [DMG-BP]   %s", stackBuf);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (hitCount >= DMGBP_MAX_HITS) {
        Log::Battle("BattleTTS: [DMG-BP] Max captures reached, disarming on hit-thread.");
        pExInfo->ContextRecord->Dr1 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x04 | (0x0F << 20));
        s_dmgBPArmed = false;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

// ============================================================================
// Arm function (mirror of GF_BP_ArmAllThreads using DR1)
// ============================================================================
//
// DR7 layout for DR1:
//   bit  2     L1   — local enable for DR1
//   bits 20-21 RW1  — condition (00 exec / 01 write / 11 r/w)
//   bits 22-23 LEN1 — length     (00 1B / 01 2B / 11 4B)
//
// We use write-only (0x01) and 2-byte (0x01).

static void Dmg_BP_ArmAllThreads()
{
    if (s_dmgBPArmed) return;

    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Log::Battle("BattleTTS: [DMG-BP] CreateToolhelp32Snapshot FAILED (err=%u)",
                    GetLastError());
        return;
    }

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    int armed = 0, failed = 0, skipped = 0;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;

            HANDLE hThread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                FALSE, te.th32ThreadID);
            if (!hThread) { failed++; continue; }

            bool isSelf = (te.th32ThreadID == myTid);
            bool isOurThread = (s_accessibilityTID != 0 &&
                                te.th32ThreadID == s_accessibilityTID);
            if (isOurThread) { CloseHandle(hThread); skipped++; continue; }
            if (!isSelf) SuspendThread(hThread);

            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(hThread, &ctx)) {
                ctx.Dr1 = DMGBP_TARGET_ADDR;
                // Clear our bits before re-setting (idempotent re-arm).
                ctx.Dr7 &= ~(0x04 | (0x0F << 20));   // clear L1 enable + cond + len
                ctx.Dr7 |= 0x04;                     // local enable DR1 (bit 2)
                ctx.Dr7 |= ((DWORD)0x01 << 20);      // condition: write-only
                ctx.Dr7 |= ((DWORD)0x01 << 22);      // length: 2 bytes (uint16)

                if (SetThreadContext(hThread, &ctx)) armed++;
                else                                  failed++;
            } else {
                failed++;
            }

            if (!isSelf) ResumeThread(hThread);
            CloseHandle(hThread);
        } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
    s_dmgBPArmed = true;
    InterlockedExchange(&s_dmgBPHitCount, 0);
    Log::Battle("BattleTTS: [DMG-BP] HW WRITE BP armed on 0x%08X (2-byte) — "
                "armed=%d failed=%d skippedOurTID=%d",
                DMGBP_TARGET_ADDR, armed, failed, skipped);
}

// ============================================================================
// Lifecycle hooks (called from battle_tts.cpp)
// ============================================================================
//
// Dmg_BP_Init       — register VEH (called once from Initialize)
// Dmg_BP_OnBattleEnter — re-arm fresh on each battle entry. Reset hit counter
//                       so each battle gets a fresh 50-hit budget.
// Dmg_BP_Shutdown   — remove VEH (called from Shutdown)

static void Dmg_BP_Init()
{
    if (s_dmgVEHHandle) return;
    s_dmgVEHHandle = AddVectoredExceptionHandler(
        1,  // FIRST handler — runs before existing GF-BP VEH (order doesn't
            // matter functionally since each checks its own DR bit, but
            // first-position is what the GF-BP code chose too)
        Dmg_BP_VectoredHandler);
    Log::Battle("BattleTTS: [DMG-BP] VEH registered: handle=0x%08X",
                (uint32_t)(uintptr_t)s_dmgVEHHandle);
}

static void Dmg_BP_OnBattleEnter()
{
    s_dmgBPArmed = false;  // allow Arm to re-fire even if disarmed mid-battle
    InterlockedExchange(&s_dmgBPHitCount, 0);
    Dmg_BP_ArmAllThreads();
}

static void Dmg_BP_Shutdown()
{
    if (s_dmgVEHHandle) {
        RemoveVectoredExceptionHandler(s_dmgVEHHandle);
        s_dmgVEHHandle = nullptr;
    }
}
