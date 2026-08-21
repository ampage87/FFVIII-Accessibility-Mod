// battle_tts_ewm_status_timers.inl (was: dispatch.inl) — the timed-status timer
// hook (sub_483470). Included from battle_tts_ewm.inl. Do not compile alone.
//
// ============================================================================
// v0.37.0 (#95): ENHANCED WAIT MODE WAS AGEING EVERY BUFF WHILE IT HELD THE ATB
// ============================================================================
//
// Aaron, after two BATs: *"when I cast Aura twice it ended before the
// character's ATB gauge filled... I am curious if the Enhanced Wait Mode system
// might not be accounting for status effects / buffs."* He was right, and the
// mechanism is a single flag.
//
// THE ENGINE KEEPS THE ATB AND THE STATUS TIMERS IN LOCKSTEP:
//
//   0x004842BD  mov byte [0x01D28DEB], 0    ; ATB update, on entry / early-out
//   0x004842EE  mov byte [0x01D28DEB], 1    ; set only when the ATB loops run
//   ...
//   0x0047D7CD  mov al, [0x01D28DEB]        ; battle state machine, state 4
//   0x0047D7D4  test al,al / je -> skip
//   0x0047D7F1  call 0x00483470             ; <- the status timers
//
// Those four are the ONLY references to 0x01D28DEB in the executable. So in
// vanilla, whenever the ATB does not advance -- Wait mode with a menu open, an
// action in progress -- the status timers do not advance either. Time is one
// clock.
//
// **EWM BREAKS THE LOCKSTEP.** HookedATBUpdate saves every ATB, calls the
// original (which sets 0x01D28DEB to 1 because its loops really did run), and
// then restores the ATB to its pre-call value. The gauge does not move, but the
// engine downstream has been told it did -- so sub_483470 runs, and every
// timed status ages on a frame where no ATB progress happened at all.
//
// A blind player spends far longer in the menus than a sighted one: reading a
// magic list, walking a target row, hearing a description. Every one of those
// seconds was being charged to Aura, Haste, Protect, Shell, Regen and Reflect
// while the gauges stood still. Fifteen seconds of Aura in the 2026-08-19 log,
// most of it frozen.
//
// THE FIX is the same shape as the ATB freeze and the GF-loading freeze that
// already sit beside it: **while the mod is holding the ATB, hold the status
// timers too.** sub_483470 does nothing else -- it is not a dispatcher, it does
// not open menus, it does not start enemy actions (see the block in
// ewm_state.inl for what it actually contains) -- so skipping it holds status
// time and touches nothing else.
//
// WHAT IS NOT DONE HERE, DELIBERATELY: 0x01D28DEB is NOT cleared. Clearing it
// would be a faithful "the ATB did not advance" signal, but the same gate also
// guards sub_482F80 on the very next instruction, and what that function does
// has not been established (it reads the battle-config byte at 0x01CFE97A and
// queues actions). Blocking a function I have not read, on a frame where the
// active character's own gauge IS still advancing, is how turns get stuck.
// One hook, on the one function whose contents are proven.
//
// v0.13.55/56 HISTORY, kept because it explains the shape of this file: those
// versions added hooks here on the belief that sub_483470 "dispatches turns".
// It does not, their own BAT said so (6 of 6 calls blocked, the enemy attack
// landed anyway), and **neither installer was ever called from anywhere** — no
// shipped build has had either hook. The sub_482F80 hook is gone rather than
// left dormant: dead code that encodes a wrong belief is worse than no code.

static void __cdecl HookedStatusTimers(void)
{
    InterlockedIncrement(&s_statusTimerCalls);
    if (s_holdStatusTimers) {
        InterlockedIncrement(&s_statusTimerHolds);
        return;               // battle time is held; statuses do not age
    }
    InterlockedIncrement(&s_statusTimerPasses);
    if (s_originalStatusTimers) {
        s_originalStatusTimers();
    }
}

static void EWM_InstallStatusTimerHook()
{
    if (s_statusTimerHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)STATUS_TIMER_FUNC_ADDR,
        (LPVOID)HookedStatusTimers,
        (LPVOID*)&s_originalStatusTimers);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)STATUS_TIMER_FUNC_ADDR);
    }
    s_statusTimerHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [STATUS-TIMER] sub_483470 hook @ 0x%08X — %s (trampoline=0x%08X)",
               STATUS_TIMER_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalStatusTimers);
}

// Read one entity's remaining duration for a given status bit. Returns -1 when
// the slot is out of range or the read faults; STATUS_TIMER_PERMANENT means the
// status has no timer at all.
static int EWM_ReadStatusTimer(int slot, int bitIndex)
{
    int v = -1;
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return -1;
    if (bitIndex < 0 || bitIndex >= STATUS_TIMER_COUNT) return -1;
    __try {
        const uint8_t* ent = (const uint8_t*)(uintptr_t)
            (BATTLE_ENTITY_ARRAY_BASE + (uintptr_t)slot * BATTLE_ENTITY_STRIDE);
        v = *(const volatile int16_t*)(ent + BENT_STATUS_TIMERS + bitIndex * 2);
    } __except (EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

// One line a second while anything is happening: how many status ticks ran, how
// many were held, and the party's Aura timers beside them. That last part is
// what makes the next BAT able to SETTLE this rather than describe it -- if the
// held count climbs while the Aura counters stand still, the hold works; if the
// counters fall while the count climbs, it does not.
static const int STATUS_BIT_AURA = 8;   // byte +0x01, bit 0x01

static void EWM_LogStatusTimerStats()
{
    DWORD now = GetTickCount();
    if (now - s_statusTimerLogTick < 1000) return;
    LONG calls = InterlockedCompareExchange(&s_statusTimerCalls, 0, 0);
    if (calls == 0 && !s_holdStatusTimers) return;
    s_statusTimerLogTick = now;

    Log::Battle("BattleTTS: [STATUS-TIMER] calls=%ld held=%ld ran=%ld hold=%d | Aura s0=%d s1=%d s2=%d",
                (long)calls,
                (long)InterlockedCompareExchange(&s_statusTimerHolds, 0, 0),
                (long)InterlockedCompareExchange(&s_statusTimerPasses, 0, 0),
                (int)s_holdStatusTimers,
                EWM_ReadStatusTimer(0, STATUS_BIT_AURA),
                EWM_ReadStatusTimer(1, STATUS_BIT_AURA),
                EWM_ReadStatusTimer(2, STATUS_BIT_AURA));
}
