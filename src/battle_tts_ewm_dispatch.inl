// battle_tts_ewm_dispatch.inl — Dispatch-layer hooks (sub_483470 + sub_482F80).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
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
//
// v0.13.56: Second dispatch-layer hook on sub_482F80. In the main battle
// loop, sub_483470 and sub_482F80 are called back-to-back under the same
// engine gate ([0x01D27B00]==0 && running-flag==1). sub_483470 appears to
// handle player-ATB-ready dispatch (opens command menus); sub_482F80
// appears to handle the execution side (starts enemy animations, damage
// impact, etc.). v0.13.55 blocked 6/6 sub_483470 calls during the bug
// window yet the enemy attack still landed, which means sub_482F80 is
// the path for enemy action execution. Hook both to close the gap.

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
