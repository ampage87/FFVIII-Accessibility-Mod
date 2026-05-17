// battle_tts_ewm_diag.inl — Diagnostic helpers (executing-phase, ATB snapshot, transition logging, turn counter).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// Contains:
//   - EWM_IsExecutingPhase: phase-byte classifier used by EWM_UpdateBattle.
//   - EWM_FormatATBSnapshot: compact "ATB=[s0=cur/max ...]" string builder.
//   - EWM_PollDiagnostics (v0.13.57): per-frame transition logger for
//     [0x01D280C0] (damage anim) / [0x01D27B00] (action-in-progress) /
//     s_ewmShouldCap, plus a post-release trace window.
//   - EWM_ResetTurnCount / EWM_LogTurnCountSummary / EWM_TrackTurnCount
//     (v0.13.58-60): per-slot ATB high→low transition counter for
//     EWM-on vs EWM-off ratio comparison.
//   - EWM_DiagLogATB: 500ms-throttled ATB-state log (capped at EWM_DIAG_MAX
//     samples per cap session).
//
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
