// battle_tts_dmg_read_bp.inl — Hardware READ BP on damage display address
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.14.6 — Find the impact-time renderer
// ============================================================================
//
// v0.14.5 BAT proved the popup struct slot at 0x01D28344 is filled ONCE by
// sub_48EF80 at action-start and NEVER modified during the event — yet the
// damage popup becomes visible at impact-time, mid-animation. The render
// pipeline must READ the slot (specifically slot[+6] = 0x01D2834A) without
// writing to it.
//
// To find the renderer, we install a HW READ breakpoint on 0x01D2834A.
// Every read of the damage value gets logged with EIP. The unique EIPs
// reveal the rendering code path; the FIRST read in each event is by
// definition the impact-time render moment.
//
// Mechanism: x86 hardware debug register DR2.
//   DR0 = GF-BP (battle_tts_ewm.inl)
//   DR1 = damage-display WRITE BP (battle_tts_dmgbp.inl)
//   DR2 = damage-display READ BP (this file)
//
// VEH checks DR6 bit 2 for our hits. Each VEH handler returns
// EXCEPTION_CONTINUE_SEARCH if the hit isn't theirs, so all three coexist.
//
// Width: 2-byte BP (uint16) — same as DMG-BP. Condition: read/write per
// the x86 spec (DR7 bits 24-25 = 0b11 means r/w; there's no read-only
// option). To filter for reads, we sample the value at the BP address and
// compare to staging — if the value matches what was just staged by
// sub_48EF80, it's a read; if it changed, it's a write. In practice the
// write path is already covered by DR1, so we treat all DR2 hits as
// "reader candidates" and rely on log analysis to filter writes by EIP
// (sub_48EF80+0x59 = 0x0048EFD9 is known).
//
// Arm timing: at OnBattleEnter. Hit-capped at 200.
//
// v0.14.7 update — FFNx noise filter:
//
// v0.14.6 BAT showed FFNx polls 0x01D2834A every frame at ~60 Hz from
// battle entry, with two distinct EIPs in FFNx DLL space (0x60000000-
// 0x80000000). The cap was burned in ~3 seconds before any damage event
// could occur. FFNx's per-frame poll is irrelevant — it's just FFNx
// mirroring engine state into its own buffers. We need the cap budget
// for vanilla FF8 reads (which are the actual render-decision moments)
// and the sub_48EF80+0x59 writes (which we already characterised in
// v0.14.2 but which now also fire on this BP).
//
// v0.14.7 filters FFNx-range EIPs out of the cap and the log. They get
// counted in a separate s_dmgReadBPFFNxHits tally, summarised at battle
// exit and at cap-reach. The first FFNx hit per battle is still logged
// once (with [DMG-READ-BP-FFNX] tag) for sanity.

static const uint32_t DMG_READ_BP_TARGET_ADDR = 0x01D2834A;  // BATTLE_DAMAGE_DISPLAY_ADDR
static const int      DMG_READ_BP_MAX_HITS    = 200;
static const uint32_t FFNX_RANGE_LO           = 0x60000000;
static const uint32_t FFNX_RANGE_HI           = 0x80000000;

static volatile bool  s_dmgReadBPArmed       = false;
static volatile LONG  s_dmgReadBPHitCount    = 0;
static volatile LONG  s_dmgReadBPFFNxHits    = 0;
static volatile LONG  s_dmgReadBPFFNxLogged  = 0;
static PVOID          s_dmgReadVEHHandle     = nullptr;

// ============================================================================
// VEH handler — DR2 hits only
// ============================================================================
//
// Mirror of the DR0 (GF-BP) and DR1 (DMG-BP) handlers. Checks DR6 bit 2,
// returns EXCEPTION_CONTINUE_SEARCH for everything else so the other VEHs
// get a chance.

static LONG CALLBACK DmgRead_BP_VectoredHandler(PEXCEPTION_POINTERS pExInfo)
{
    if (pExInfo->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD dr6 = (DWORD)pExInfo->ContextRecord->Dr6;
    if (!(dr6 & 0x04))
        return EXCEPTION_CONTINUE_SEARCH;  // DR2 bit not set — not our hit

    // Acknowledge by clearing the low 4 bits of DR6.
    pExInfo->ContextRecord->Dr6 &= ~0x0F;

    // Skip our own polling thread (defensive — we DO read 0x01D2834A from
    // FlushHPAnnouncements, so this BP CAN fire on our thread; we explicitly
    // don't want those captures).
    if (s_accessibilityTID != 0 && GetCurrentThreadId() == s_accessibilityTID) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    DWORD eip = (DWORD)pExInfo->ContextRecord->Eip;
    DWORD tid = GetCurrentThreadId();

    // v0.14.7: FFNx noise filter. FFNx polls 0x01D2834A every frame from
    // battle entry; if we count those hits the cap burns out before any
    // damage event. Skip them silently (with a counter for visibility) so
    // the cap budget is preserved for vanilla FF8 reads + writes.
    if (eip >= FFNX_RANGE_LO && eip < FFNX_RANGE_HI) {
        LONG ffnxHits = InterlockedIncrement(&s_dmgReadBPFFNxHits);
        // Log the very first FFNx hit per battle so we have evidence FFNx
        // polling is occurring; suppress all subsequent ones.
        if (InterlockedCompareExchange(&s_dmgReadBPFFNxLogged, 1, 0) == 0) {
            Log::Battle("BattleTTS: [DMG-READ-BP-FFNX] First FFNx hit (suppressed thereafter): "
                        "TID=%u EIP=0x%08X — FFNx is polling 0x01D2834A; "
                        "these hits are excluded from the cap.",
                        tid, eip);
            (void)ffnxHits;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    LONG hitCount = InterlockedIncrement(&s_dmgReadBPHitCount);
    if (hitCount > DMG_READ_BP_MAX_HITS) {
        // Disarm DR2 in this thread's context.
        pExInfo->ContextRecord->Dr2 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x10 | (0x0F << 24));  // L2 + cond + len
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Capture the value at the BP address (after the access).
    uint16_t curVal = 0;
    __try { curVal = *(uint16_t*)DMG_READ_BP_TARGET_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Capture all GP registers — the value typically lands in EAX/ECX/EDX
    // after a read instruction.
    DWORD regEax = (DWORD)pExInfo->ContextRecord->Eax;
    DWORD regEbx = (DWORD)pExInfo->ContextRecord->Ebx;
    DWORD regEcx = (DWORD)pExInfo->ContextRecord->Ecx;
    DWORD regEdx = (DWORD)pExInfo->ContextRecord->Edx;
    DWORD regEsi = (DWORD)pExInfo->ContextRecord->Esi;
    DWORD regEdi = (DWORD)pExInfo->ContextRecord->Edi;
    DWORD regEbp = (DWORD)pExInfo->ContextRecord->Ebp;
    DWORD regEsp = (DWORD)pExInfo->ContextRecord->Esp;

    // Code bytes around EIP. The triggering instruction precedes EIP. For a
    // read like `mov dx, [imm32]` (4–7 bytes), the instruction starts 4–7
    // bytes before EIP. Capture 16 before + 16 at/after.
    uint8_t codeBuf[32] = {};
    DWORD codeStart = (eip >= 16) ? eip - 16 : 0;
    __try { memcpy(codeBuf, (uint8_t*)codeStart, 32); } __except(EXCEPTION_EXECUTE_HANDLER) {}

    char hexBuf[128] = {};
    int p = 0;
    for (int i = 0; i < 32; i++)
        p += snprintf(hexBuf + p, sizeof(hexBuf) - p, "%02X ", codeBuf[i]);

    Log::Battle("BattleTTS: [DMG-READ-BP] #%ld ACCESS 0x%08X! TID=%u EIP=0x%08X val@addr=%u",
                hitCount, DMG_READ_BP_TARGET_ADDR, tid, eip, (unsigned)curVal);
    Log::Battle("BattleTTS: [DMG-READ-BP]   regs: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X",
                regEax, regEbx, regEcx, regEdx);
    Log::Battle("BattleTTS: [DMG-READ-BP]   regs: ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X",
                regEsi, regEdi, regEbp, regEsp);
    Log::Battle("BattleTTS: [DMG-READ-BP]   code[0x%08X..]: %s",
                codeStart, hexBuf);

    // Stack walk for return addresses in FF8 main exe + FFNx DLL space.
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
            Log::Battle("BattleTTS: [DMG-READ-BP]   %s", stackBuf);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (hitCount >= DMG_READ_BP_MAX_HITS) {
        LONG ffnx = InterlockedCompareExchange(&s_dmgReadBPFFNxHits, 0, 0);
        Log::Battle("BattleTTS: [DMG-READ-BP] Max captures reached, disarming on hit-thread. "
                    "FFNx-filtered hits this battle: %ld", ffnx);
        pExInfo->ContextRecord->Dr2 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x10 | (0x0F << 24));
        s_dmgReadBPArmed = false;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

// ============================================================================
// Arm function (DR2 with READ/WRITE condition)
// ============================================================================
//
// DR7 layout for DR2:
//   bit  4     L2   — local enable for DR2
//   bits 24-25 RW2  — condition (00 exec / 01 write / 11 r/w)
//   bits 26-27 LEN2 — length     (00 1B / 01 2B / 11 4B)
//
// x86 has no "read-only" condition — only write-only or read/write.
// Use 0b11 (read/write) and rely on EIP analysis to separate read sites
// from write sites. The known write site is sub_48EF80+0x59 = 0x0048EFD9;
// any other EIP is a read.

static void DmgRead_BP_ArmAllThreads()
{
    if (s_dmgReadBPArmed) return;

    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Log::Battle("BattleTTS: [DMG-READ-BP] CreateToolhelp32Snapshot FAILED (err=%u)",
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
                ctx.Dr2 = DMG_READ_BP_TARGET_ADDR;
                ctx.Dr7 &= ~(0x10 | (0x0F << 24));   // clear L2 enable + cond + len
                ctx.Dr7 |= 0x10;                     // local enable DR2 (bit 4)
                ctx.Dr7 |= ((DWORD)0x03 << 24);      // condition: read/write
                ctx.Dr7 |= ((DWORD)0x01 << 26);      // length: 2 bytes (uint16)

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
    s_dmgReadBPArmed = true;
    InterlockedExchange(&s_dmgReadBPHitCount, 0);
    Log::Battle("BattleTTS: [DMG-READ-BP] HW READ/WRITE BP armed on 0x%08X (2-byte) — "
                "armed=%d failed=%d skippedOurTID=%d",
                DMG_READ_BP_TARGET_ADDR, armed, failed, skipped);
}

// ============================================================================
// Lifecycle
// ============================================================================

static void DmgRead_BP_Init()
{
    if (s_dmgReadVEHHandle) return;
    s_dmgReadVEHHandle = AddVectoredExceptionHandler(1, DmgRead_BP_VectoredHandler);
    Log::Battle("BattleTTS: [DMG-READ-BP] VEH registered: handle=0x%08X",
                (uint32_t)(uintptr_t)s_dmgReadVEHHandle);
}

static void DmgRead_BP_OnBattleEnter()
{
    s_dmgReadBPArmed = false;
    InterlockedExchange(&s_dmgReadBPHitCount, 0);
    InterlockedExchange(&s_dmgReadBPFFNxHits, 0);
    InterlockedExchange(&s_dmgReadBPFFNxLogged, 0);
    DmgRead_BP_ArmAllThreads();
}

static void DmgRead_BP_Shutdown()
{
    if (s_dmgReadVEHHandle) {
        RemoveVectoredExceptionHandler(s_dmgReadVEHHandle);
        s_dmgReadVEHHandle = nullptr;
    }
}
