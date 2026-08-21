// battle_tts_ewm_update.inl — EWM_UpdateBattle (per-frame freeze state machine).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// Called every frame during battle from BattleTTS::Update() (mod thread).
// Sets s_ewmShouldCap + s_ewmCapExcludeSlot which the game-thread hook reads.
//
// ATB CAPPING (v0.10.55): Instead of freezing ATB entirely, we let ATB fill
// at normal Speed-based rates but cap all entities at ATB_max - 1. This means:
//   - Nobody can trigger a new turn while the player is deciding
//   - Speed-based turn ratios are fully preserved (no extra player turns)
//   - When the cap lifts, the fastest entity triggers next (within 1 tick)
// The deciding character is excluded from capping (their ATB is already at max).
//
// MUST come last in the ewm.inl chain — calls helpers defined in gf_patch.inl
// (EWM_ClampGFState, EWM_RestoreGFPatch) and diag.inl (EWM_PollDiagnostics,
// EWM_IsExecutingPhase, EWM_DiagLogATB).

static void EWM_UpdateBattle()
{
    if (!s_ewmEnabled || !s_ewmHookInstalled) {
        if (s_ewmFreezing) {
            EWM_SetFreeze(false);
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

    // v0.37.0 (#95): HOLD THE STATUS TIMERS EXACTLY WHEN THE ATB IS HELD.
    // (This replaced a v0.13.55 comment describing `s_blockProcessReady`, a
    // flag maintained faithfully for eleven versions to gate a hook that was
    // never installed, on a function that was never the dispatcher it was
    // named after. See battle_tts_ewm_state.inl.)
    // The engine keeps the two on one clock (0x01D28DEB, set by the ATB update
    // and read by the gate at 0x0047D7CD); EWM broke that by letting the ATB
    // function run and then undoing its work, so every frozen frame still aged
    // every buff. This is the other half of the freeze, and it is assigned
    // BELOW, once s_ewmShouldCap has been decided for this frame.

    // If any transition-hold signal is active AND nobody is deciding yet,
    // force the cap on EVERY slot (excludeSlot=0xFF). If a player was already
    // deciding (activeChar < 3) the command menu is already visible and the
    // normal cap logic below handles the rest — no need for a full-everyone
    // cap that would demote the active character's ATB.
    if (damageOrActionActive && activeChar == 0xFF) {
        s_ewmCapGF = true;
        EWM_ClampGFState();
        s_ewmCapExcludeSlot = 0xFF;
        EWM_SetFreeze(true);
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
            EWM_SetFreeze(true);
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
                EWM_SetFreeze(true);
                EWM_DiagLogATB("cap");  // v0.10.57: log ATB values while capped
            } else {
                // Phase says executing — but check grace period first
                if (s_ewmNewTurnGrace) {
                    // Still in grace period: phase=34 is STALE from previous character.
                    // Keep cap active until we see a non-executing phase.
                    EWM_SetFreeze(true);
                } else {
                    // Action executing (no grace) — release cap
                    if (s_ewmFreezing) {
                        s_ewmFreezing = false;
                        EWM_SetFreeze(false);
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
            EWM_SetFreeze(false);
            s_ewmCapExcludeSlot = 0xFF;
            Log::Battle("BattleTTS: [EWM] ATB cap released (no turn active, gfLoading=%d)",
                       (int)gfIsLoading);
        }
    }
}

// ============================================================================
// Turn announcement + Command menu TTS (v0.10.11)
// ============================================================================
