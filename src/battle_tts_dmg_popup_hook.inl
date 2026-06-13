// battle_tts_dmg_popup_hook.inl — MinHook on sub_48EF80 (damage popup creator)
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.14.3 — Damage-popup-create signal (diagnostic)
// ============================================================================
//
// v0.14.2 BAT proved every visible damage popup is created via a single engine
// function — `sub_48EF80` — which copies a 24-byte struct from a staging area
// (0x01D27ADC..) into the next slot of an array starting at 0x01D28344. The
// damage value lives at staging offset 0x01D27AE4 (uint16); it lands at
// slot[+6]. Hardware-BP cross-reference showed the call fires at ANIM-UP
// time (popup creation), not at ANIM-DOWN (popup despawn — current v0.13.90
// trigger). All three known caller chains converge here:
//
//   Chain A: sub_485420 → sub_485610 → sub_48E830 → sub_48EF80   (most hits)
//   Chain B: sub_47CF50 → sub_485030 → sub_48EF80                (rare; bypasses sub_48E830)
//   Chain C: sub_487DF0 → sub_48E830 → sub_48EF80                (a few hits)
//
// Hooking sub_48EF80 entry catches all three with a single MinHook. Reading
// staging[0x01D27AE4] at hook entry gives us the damage value BEFORE the copy,
// so we can distinguish damage popups (dmg>0) from status / clear popups
// (dmg=0) without false-positive announcements.
//
// Function prologue (14 linear bytes, no branches — fits MinHook's 5-byte JMP):
//
//   0x0048EF80:  mov   cl, byte ptr [0x01D280C1]      ; load slot counter
//   0x0048EF86:  xor   eax, eax
//   0x0048EF88:  mov   al, cl                          ; al = old counter
//   0x0048EF8A:  mov   dl, byte ptr [0x01D27ADC]      ; staging byte 0
//   0x0048EF90:  inc   cl                              ; advance counter
//   0x0048EF92:  lea   eax, [eax + eax*2]              ; eax = old_cl * 3
//
// Calling convention: __cdecl with one arg passed at [esp+4] (read as byte).
// Caller cleans the stack. Hook signature mirrors this.
//
// DIAGNOSTIC ONLY in v0.14.3. Production trigger remains v0.13.90 anim-flag-fall.
// Promotion to production trigger happens in v0.14.4 once BAT confirms 1:1
// correlation across all five damage cases (Squall physical, Quistis Fire,
// Cure injured, Cure full-HP, miss) and no false positives.

static const uint32_t SUB_48EF80_ADDR        = 0x0048EF80;
static const uint32_t DMG_POPUP_STAGING_DMG  = 0x01D27AE4;  // uint16
static const uint32_t DMG_POPUP_STAGING_BASE = 0x01D27ADC;  // staging area start
static const uint32_t DMG_POPUP_SLOT_COUNTER = 0x01D280C1;  // pre-call slot index

typedef void (__cdecl *Sub48EF80Fn)(int arg0);
static Sub48EF80Fn s_originalSub48EF80 = nullptr;
static bool        s_dmgPopupHookInstalled = false;

// Cross-reference signal — read by Update() / FlushHPAnnouncements consumers.
// v0.14.4: s_lastDmgPopupTick and s_lastDmgPopupValue moved to
// battle_tts_hp.inl (which is #included BEFORE this file) so PollHPChanges
// can read them. We just write to them here on the game thread.
static volatile LONG  s_dmgPopupHookCount      = 0;  // every entry, regardless of dmg
static volatile LONG  s_dmgPopupHookDmgCount   = 0;  // only when staging dmg > 0
static volatile LONG  s_dmgPopupHookZeroCount  = 0;  // only when staging dmg == 0

// ============================================================================
// Hook body
// ============================================================================
//
// Runs on the game thread, on every call to sub_48EF80. CRITICAL: must always
// call the original before returning, otherwise the engine's slot array won't
// get filled and the popup pipeline will be broken.

static void __cdecl HookedSub48EF80(int arg0)
{
    InterlockedIncrement(&s_dmgPopupHookCount);

    // Read staging area BEFORE the original runs. The original copies these
    // values into the slot, but we want the damage value here so we can
    // classify the call (damage vs status/clear) without trusting the slot
    // array (which the original may overwrite on subsequent calls).
    uint16_t stagingDmg = 0;
    uint8_t  stagingB0  = 0, stagingB1 = 0, stagingB2 = 0;
    uint8_t  slotCounterPre = 0;
    __try {
        slotCounterPre = *(uint8_t*)DMG_POPUP_SLOT_COUNTER;
        stagingB0      = *(uint8_t*)(DMG_POPUP_STAGING_BASE + 0);
        stagingB1      = *(uint8_t*)(DMG_POPUP_STAGING_BASE + 1);
        stagingB2      = *(uint8_t*)(DMG_POPUP_STAGING_BASE + 2);
        stagingDmg     = *(uint16_t*)DMG_POPUP_STAGING_DMG;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // ALWAYS pass through to the original. Never short-circuit — the engine
    // depends on the slot array being filled even for status / clear popups.
    s_originalSub48EF80(arg0);

    if (stagingDmg > 0) {
        InterlockedIncrement(&s_dmgPopupHookDmgCount);
        InterlockedExchange((volatile LONG*)&s_lastDmgPopupTick,
                            (LONG)GetTickCount());
        InterlockedExchange((volatile LONG*)&s_lastDmgPopupValue,
                            (LONG)stagingDmg);
        DiagLogBattle("BattleTTS: [DMG-POPUP-CREATE] #%ld dmg=%u arg0=0x%02X "
                    "slotCntPre=%u staging[0..2]=%02X %02X %02X tick=%u",
                    s_dmgPopupHookDmgCount,
                    (unsigned)stagingDmg,
                    (unsigned)(arg0 & 0xFF),
                    (unsigned)slotCounterPre,
                    (unsigned)stagingB0,
                    (unsigned)stagingB1,
                    (unsigned)stagingB2,
                    (unsigned)GetTickCount());
    } else {
        InterlockedIncrement(&s_dmgPopupHookZeroCount);
        // Don't log every zero-write — they fire on status/clear popups and
        // would flood the log. Counter-only is enough to size the noise.
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

static void DmgPopupHook_Install()
{
    if (s_dmgPopupHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)SUB_48EF80_ADDR,
        (LPVOID)HookedSub48EF80,
        (LPVOID*)&s_originalSub48EF80);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)SUB_48EF80_ADDR);
    }
    s_dmgPopupHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [DMG-POPUP-HOOK] sub_48EF80 hook @ 0x%08X — %s "
                "(trampoline=0x%08X)",
                SUB_48EF80_ADDR, MH_StatusToString(st),
                (uint32_t)(uintptr_t)s_originalSub48EF80);
}

static void DmgPopupHook_Reset()
{
    InterlockedExchange(&s_dmgPopupHookCount,     0);
    InterlockedExchange(&s_dmgPopupHookDmgCount,  0);
    InterlockedExchange(&s_dmgPopupHookZeroCount, 0);
    InterlockedExchange((volatile LONG*)&s_lastDmgPopupTick,  0);
    InterlockedExchange((volatile LONG*)&s_lastDmgPopupValue, 0);
    // v0.14.4: Also reset the mod-thread "seen" tick so the next battle
    // doesn't carry leftover state from this one.
    InterlockedExchange((volatile LONG*)&s_lastSeenDmgPopupTick, 0);
}

// Called from Update() periodically to log running totals. Helps verify the
// hook is firing (and at what rate) even when the per-event lines aren't
// caught in the analysis window.
static DWORD s_dmgPopupStatsLastTick = 0;

static void DmgPopupHook_LogStats()
{
    DWORD now = GetTickCount();
    if (now - s_dmgPopupStatsLastTick < 5000) return;  // every 5 s
    s_dmgPopupStatsLastTick = now;

    LONG total = s_dmgPopupHookCount;
    LONG dmg   = s_dmgPopupHookDmgCount;
    LONG zero  = s_dmgPopupHookZeroCount;
    if (total == 0) return;  // hook never fired this window — skip log
    DiagLogBattle("BattleTTS: [DMG-POPUP-HOOK] STATS total=%ld dmgPopups=%ld "
                "zeroPopups=%ld lastDmgTick=%u lastDmgVal=%u",
                total, dmg, zero,
                (unsigned)s_lastDmgPopupTick,
                (unsigned)s_lastDmgPopupValue);
}
