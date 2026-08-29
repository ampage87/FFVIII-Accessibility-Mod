// battle_tts_countdown.inl -- speaking the digit that floats over a battle actor
//
// v0.108.0 (#megaflare). The arithmetic, the addresses and the reasoning are in
// battle_countdown_model.inl, which is pure and tested. This file is the part
// that touches battle memory and the screen reader.
//
// Polled rather than hooked, deliberately. `sub_50A500` has exactly two call
// sites, both inside `sub_50A410`, and both hand it a word straight out of the
// entity's status-timer array -- so the value on screen and the value in memory
// are the same word, and reading it costs two SEH-guarded int16 loads per actor
// per frame instead of a detour through the render path. There is nothing a
// hook would catch that the poll misses.

// The actor array `sub_50A410` walks, used ONLY for the diagnostic line: the
// engine gates each counter on a flag there before it draws. The mod does not
// gate the announcement on it -- a counter that exists but is momentarily
// clipped off screen is still a counter the player needs to know about, and one
// address whose base could be wrong is one address that could silence the
// feature. So: read it, log it, never let it decide.
static const uint32_t BC_ACTOR_ARRAY_BASE   = 0x01D972C0;
static const uint32_t BC_ACTOR_STRIDE       = 0x9C;
static const uint32_t BC_ACTOR_FLAGS_OFFSET = 0x08;
static const uint32_t BC_ACTOR_FLAG_PETRIFY = 0x2000;    // white, entity + 0x64
static const uint32_t BC_ACTOR_FLAG_DOOM    = 0x80000;   // blue,  entity + 0x60

static const int BC_SLOTS[2] = { BC_TIMER_DOOM, BC_TIMER_GRADUAL_PETRIFY };
static const int BC_SLOT_N   = 2;

static bool  s_bcWasLive[BATTLE_TOTAL_SLOTS][BC_SLOT_N] = {};
static int   s_bcLastDigit[BATTLE_TOTAL_SLOTS][BC_SLOT_N] = {};
static DWORD s_bcSweepUntil = 0;     // the Mega Flare diagnostic window
static DWORD s_bcSweepTick  = 0;

// One int16 out of the entity's status-timer array, with an explicit ok rather
// than an in-band error value -- every int16 is a legal timer, so there is no
// number this could return that means "the read failed".
static bool BcReadTimer(int slot, int bitIndex, int* out)
{
    bool ok = false;
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (bitIndex < 0 || bitIndex >= STATUS_TIMER_COUNT) return false;
    __try {
        const uint8_t* ent = (const uint8_t*)(uintptr_t)
            (BATTLE_ENTITY_ARRAY_BASE + (uintptr_t)slot * BATTLE_ENTITY_STRIDE);
        *out = *(const volatile int16_t*)(ent + BENT_STATUS_TIMERS + bitIndex * 2);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static bool BcReadActorFlags(int slot, uint32_t* out)
{
    bool ok = false;
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    __try {
        *out = *(const volatile uint32_t*)(uintptr_t)
            (BC_ACTOR_ARRAY_BASE + (uintptr_t)slot * BC_ACTOR_STRIDE
             + BC_ACTOR_FLAGS_OFFSET);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static void Countdown_OnBattleEnter()
{
    memset(s_bcWasLive, 0, sizeof(s_bcWasLive));
    for (int s = 0; s < BATTLE_TOTAL_SLOTS; s++)
        for (int k = 0; k < BC_SLOT_N; k++) s_bcLastDigit[s][k] = -1;
    s_bcSweepUntil = 0;
    s_bcSweepTick  = 0;
}

// Armed by NoteBattleWindowText when "Mega Flare" reaches a battle window.
// For the next twenty seconds the whole fourteen-slot timer array is dumped
// once a second for every actor. If a digit appears on screen and NOTHING in
// this dump is live, the countdown is not engine text and not an engine number
// -- it is a texture inside the effect animation, and that is the answer.
static const DWORD BC_SWEEP_MS = 20000;

static void Countdown_ArmSweep(const char* why)
{
    s_bcSweepUntil = GetTickCount() + BC_SWEEP_MS;
    s_bcSweepTick  = 0;
    Log::Battle("BattleTTS: [COUNTDOWN-SWEEP] armed for %u ms by \"%s\"",
                (unsigned)BC_SWEEP_MS, why ? why : "");
}

static void Countdown_Sweep()
{
    DWORD now = GetTickCount();
    if (s_bcSweepUntil == 0 || (LONG)(now - s_bcSweepUntil) > 0) return;
    if (s_bcSweepTick != 0 && (now - s_bcSweepTick) < 1000) return;
    s_bcSweepTick = now;

    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        char line[256];
        int  pos = 0;
        int  live = 0;
        for (int b = 0; b < STATUS_TIMER_COUNT; b++) {
            int raw = 0;
            if (!BcReadTimer(slot, b, &raw)) { raw = BC_NO_TIMER; }
            if (BcTimerLive(raw)) {
                live++;
                if (pos < (int)sizeof(line) - 24) {
                    pos += snprintf(line + pos, sizeof(line) - pos,
                                    "%sb%d=%d", pos ? " " : "", b, raw);
                }
            }
        }
        if (live == 0) continue;
        uint32_t flags = 0;
        bool flagsOk = BcReadActorFlags(slot, &flags);
        Log::Battle("BattleTTS: [COUNTDOWN-SWEEP] slot%d actorFlags=%s%08X | %s",
                    slot, flagsOk ? "" : "(bad)", (unsigned)flags, line);
    }
}

// Every battle frame. Two int16 reads per actor, then nothing at all unless the
// number on screen has changed.
static void Countdown_Poll()
{
    Countdown_Sweep();

    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        for (int k = 0; k < BC_SLOT_N; k++) {
            const int bit = BC_SLOTS[k];
            int raw = 0;
            if (!BcReadTimer(slot, bit, &raw)) {
                s_bcWasLive[slot][k] = false;
                continue;
            }
            const bool live  = BcTimerLive(raw);
            const int  digit = BcDigitFor(raw);

            // v0.110.0: the engine's own draw gate. At battle entry the whole
            // timer array reads back as zero, which is not the sentinel -- so
            // without this every slot is "a counter at 0" and the mod opens
            // three battles a session shouting "Alert, Countdown 0".
            uint32_t flags = 0;
            const bool     flagsOk = BcReadActorFlags(slot, &flags);
            const uint32_t want    = (bit == BC_TIMER_DOOM) ? BC_ACTOR_FLAG_DOOM
                                                            : BC_ACTOR_FLAG_PETRIFY;
            const bool drawn = BcIsDrawn(live, flagsOk, flags, want);

            if (BcShouldAnnounce(drawn, digit, s_bcWasLive[slot][k],
                                 s_bcLastDigit[slot][k])) {
                char msg[64];
                BcAnnounceText(msg, sizeof(msg), digit);
                Log::Battle("BattleTTS: [COUNTDOWN] slot%d %s raw=%d digit=%d "
                            "actorFlags=%s%08X -- \"%s\"",
                            slot, BcSlotName(bit), raw, digit,
                            flagsOk ? "" : "(bad)", (unsigned)flags, msg);

                // Aaron: "This also needs to be assertive and interrupt any
                // other text so it can't be missed." Interrupt, every time.
                // The utterance is four words precisely so that the next one,
                // a second later, is not cutting off something half-said.
                ScreenReader::Speak(msg, true);
            } else if (live && !drawn) {
                // Live but not drawn. Once a second at most, and only so that a
                // wrong actor-array base shows up as a stream of these rather
                // than as a feature that never fires.
                static DWORD s_heldTick = 0;
                DWORD nowHeld = GetTickCount();
                if (nowHeld - s_heldTick >= 1000) {
                    s_heldTick = nowHeld;
                    Log::Battle("BattleTTS: [COUNTDOWN-HELD] slot%d %s raw=%d "
                                "digit=%d actorFlags=%s%08X (engine draws nothing)",
                                slot, BcSlotName(bit), raw, digit,
                                flagsOk ? "" : "(bad)", (unsigned)flags);
                }
            }

            s_bcWasLive[slot][k]   = drawn;
            s_bcLastDigit[slot][k] = drawn ? digit : -1;
        }
    }
}
