// battle_tts_dmg_render_hook.inl — MinHook on sub_5068B0 (impact-time popup render)
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.14.8 — Hook the impact-time renderer
// ============================================================================
//
// v0.14.7 BAT identified sub_5068B0 as the impact-time popup-render entry
// point. With FFNx noise filtered out, the DR2 read BP captured 8 hits at
// EIP 0x00506924 (= sub_5068B0+0x74), exactly one per damage event, val
// matching the damage value, timestamp aligning with [ROI-LIVE-SHADOW]
// YELLOW spike (visible-on-screen). The reader at +0x74 is
//
//     mov si, word ptr [edi+6]    ; edi = popup slot ptr, +6 = damage low word
//
// Function prologue:
//
//     0x005068B0: push ebx                       ; 1
//     0x005068B1: push edi                       ; 1
//     0x005068B2: mov edi, [esp+0xC]             ; 4   first arg = slot ptr
//     0x005068B6: test [edi+2], 1                ; 4
//     0x005068BA: je 0x5068C6                    ; early exit if !active
//
// 10 linear bytes before the first branch — plenty for a 5-byte MinHook
// patch. Calling convention assumed __cdecl (MSVC default for FF8-era C).
// One arg, popup slot pointer.
//
// Hook strategy (v0.14.8 = diagnostic only, NO production trigger yet):
//   - At entry, read slot[+0..+7] BEFORE original runs.
//   - Classify by (active_flag & 1) and damage value (slot[+6]):
//       active=1, dmg>0  -> impact-time damage popup (the signal we want)
//       active=1, dmg==0 -> status popup or zero-damage hit
//       active=0         -> early-exit case (slot inactive)
//   - For impact-time damage popups: log [DMG-RENDER] and publish
//     s_lastDmgRenderTick / s_lastDmgRenderValue for the mod thread.
//   - For other classes: count silently, periodic STATS log.
//   - Always pass through to original.
//
// Production trigger remains v0.13.90 anim-flag-fall in v0.14.8. Promotion
// happens in v0.14.9 only after BAT confirms this hook fires at impact
// time across all five damage cases (Squall physical, Quistis Fire, Cure
// injured, Cure full-HP, miss). v0.14.4 was the lesson: never promote on
// partial verification.

static const uint32_t SUB_5068B0_ADDR = 0x005068B0;

typedef int (__cdecl *RenderPopupSlot_fn)(void* slotPtr);
static RenderPopupSlot_fn s_origRenderPopupSlot = nullptr;
static bool s_dmgRenderHookInstalled = false;

// Mod-thread-visible state: s_lastDmgRenderTick / s_lastDmgRenderValue
// are DEFINED in battle_tts_hp.inl (which is #included before this file),
// because PollHPChanges in hp.inl needs to read them. The hit counters
// live here.
volatile LONG  s_dmgRenderHitsActive   = 0;  // active=1, dmg>0
volatile LONG  s_dmgRenderHitsZero     = 0;  // active=1, dmg==0
volatile LONG  s_dmgRenderHitsInactive = 0;  // active=0
volatile DWORD s_dmgRenderLastStatsTick = 0;

static int __cdecl Hooked_RenderPopupSlot(void* slotPtr)
{
    // v0.14.10 — read slot fields from the arg pointer (the slot being
    // rendered this call), not from slot 0 directly. v0.14.9 BAT proved
    // sub_5068B0's arg IS a popup-slot pointer (arg=0x01D28344 for slot 0,
    // arg=0x01D2835C for slot 1; offset 0x18 = the 24-byte slot stride).
    // The active-flag check that v0.14.8 used was inverted: damage popups
    // have slot[+2]=0x00, idle slots have slot[+2]=0x01. The function's
    // own prologue 'test [edi+2], 1 / je' is a SKIP test; the body runs
    // when bit 0 is CLEAR. We don't replicate the test; we just check
    // slot_dmg > 0 which is unambiguous.
    uint8_t  slotBytes[16] = {};
    bool     slotReadOk = false;

    __try {
        if (slotPtr) {
            memcpy(slotBytes, slotPtr, sizeof(slotBytes));
            slotReadOk = true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    uint8_t  slot_arg0   = slotReadOk ? slotBytes[0] : 0;
    uint8_t  slot_active = slotReadOk ? slotBytes[2] : 0;
    uint16_t slot_dmg    = slotReadOk ? *(uint16_t*)(slotBytes + 6) : 0;
    (void)slot_arg0;

    if (slot_dmg > 0) {
        // The impact-time signal. Publish to mod thread + log.
        InterlockedIncrement(&s_dmgRenderHitsActive);
        DWORD nowTick = GetTickCount();
        InterlockedExchange((LONG*)&s_lastDmgRenderTick, (LONG)nowTick);
        InterlockedExchange((LONG*)&s_lastDmgRenderValue, (LONG)slot_dmg);
        DiagLogBattle("BattleTTS: [DMG-RENDER] slot=0x%p dmg=%u active=0x%02X tick=%u",
                    slotPtr, (unsigned)slot_dmg, (unsigned)slot_active,
                    (unsigned)nowTick);
    } else if ((slot_active & 0x01) != 0) {
        // Idle slot — bit 0 set, the function will early-exit.
        InterlockedIncrement(&s_dmgRenderHitsInactive);
    } else {
        // Active slot but zero damage — status popup or zero-damage hit.
        InterlockedIncrement(&s_dmgRenderHitsZero);
    }

    return s_origRenderPopupSlot(slotPtr);
}

static void DmgRenderHook_Install()
{
    if (s_dmgRenderHookInstalled) return;

    MH_STATUS status = MH_CreateHook((LPVOID)SUB_5068B0_ADDR,
                                     (LPVOID)&Hooked_RenderPopupSlot,
                                     (LPVOID*)&s_origRenderPopupSlot);
    if (status != MH_OK) {
        Log::Battle("BattleTTS: [DMG-RENDER-HOOK] MH_CreateHook FAILED status=%d", (int)status);
        return;
    }

    status = MH_EnableHook((LPVOID)SUB_5068B0_ADDR);
    if (status != MH_OK) {
        Log::Battle("BattleTTS: [DMG-RENDER-HOOK] MH_EnableHook FAILED status=%d", (int)status);
        return;
    }

    s_dmgRenderHookInstalled = true;
    Log::Battle("BattleTTS: [DMG-RENDER-HOOK] MH_OK \u2014 hooked sub_5068B0 (=0x%08X) "
                "for impact-time render diagnostic.", SUB_5068B0_ADDR);
}

static void DmgRenderHook_Reset()
{
    InterlockedExchange((LONG*)&s_lastDmgRenderTick, 0);
    InterlockedExchange((LONG*)&s_lastDmgRenderValue, 0);
    InterlockedExchange((LONG*)&s_lastSeenDmgRenderTick, 0);  // v0.14.10
    InterlockedExchange(&s_dmgRenderHitsActive, 0);
    InterlockedExchange(&s_dmgRenderHitsZero, 0);
    InterlockedExchange(&s_dmgRenderHitsInactive, 0);
    InterlockedExchange((LONG*)&s_dmgRenderLastStatsTick, (LONG)GetTickCount());
}

// Called periodically from Update() to summarise hook activity. Helps
// distinguish "hook never fired" from "hook fired but classification is
// wrong" if the BAT shows unexpected behaviour.
static void DmgRenderHook_PeriodicStats()
{
    if (!s_dmgRenderHookInstalled) return;
    DWORD nowTick = GetTickCount();
    DWORD lastStats = (DWORD)InterlockedCompareExchange(
        (LONG*)&s_dmgRenderLastStatsTick, 0, 0);
    if (nowTick - lastStats < 5000) return;
    InterlockedExchange((LONG*)&s_dmgRenderLastStatsTick, (LONG)nowTick);

    LONG a = InterlockedCompareExchange(&s_dmgRenderHitsActive,   0, 0);
    LONG z = InterlockedCompareExchange(&s_dmgRenderHitsZero,     0, 0);
    LONG i = InterlockedCompareExchange(&s_dmgRenderHitsInactive, 0, 0);
    if (a + z + i == 0) return;  // Quiet period \u2014 nothing to report.

    DiagLogBattle("BattleTTS: [DMG-RENDER-HOOK] STATS active+dmg=%ld active+zero=%ld inactive=%ld "
                "lastTick=%u lastVal=%u",
                a, z, i,
                (unsigned)InterlockedCompareExchange((LONG*)&s_lastDmgRenderTick,  0, 0),
                (unsigned)InterlockedCompareExchange((LONG*)&s_lastDmgRenderValue, 0, 0));
}
