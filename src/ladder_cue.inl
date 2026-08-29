// ============================================================================
// v0.124.0 (#centra): THE LADDER CLIMB SOUND -- THE GAME'S OWN
// ============================================================================
// Aaron: "why can't we utilize the same climbing sound as the rest of the
// ladders in the game?" We can. v0.123.0 synthesised knocks on the assumption
// that reaching the engine's audio would be fragile; the assumption was never
// tested and it was wrong. See ladder_cue_model.inl for the routine, its four
// call sites, and the branch that picks the ladder sound from the movement
// mode -- which means the mod chooses no sound, no id and no volume. It asks
// the engine for the noise this character on this surface would make.
//
// Nothing here reads the entity array or the player index: the caller decides
// which entity is climbing and passes its block through. That is what lets
// tests/ladder_cue_compile.cpp build this unit on the host, which
// field_navigation.cpp's own translation unit cannot be.

typedef void (__cdecl* LadderStepSoundFn)(int entityPtr, int foot, int volume, int pan);

static DWORD s_ladderStepLastMs  = 0;
static bool  s_ladderStepEver    = false;
static int   s_ladderStepIndex   = 0;
static DWORD s_ladderClimbStartMs = 0;

// v0.125.0: what the engine is doing on THIS climb, cleared the frame the
// movement mode turns into a ladder mode. Scoped to the climb on purpose --
// steps the engine played on the previous ladder must not silence this one.
static DWORD s_ladderEngineLastMs = 0;
static bool  s_ladderEngineEver   = false;

// The learned cadence, carried across climbs. One engine-sounded ladder
// anywhere in the game teaches the mod the game's own rhythm, and the Centra
// tower ladders come before the roof one on the only route up.
static unsigned s_ladderIntervalMs = LADDER_STEP_MS_DEFAULT;
static bool     s_ladderLearned    = false;

// SEH-guarded compare of sub_00520260's prologue, run BEFORE MinHook is pointed
// at it -- the install overwrites the bytes this reads. A patched or repacked
// exe fails it and the measurement hook is simply not installed.
//
// v0.131.8 dropped the re-entrancy flag that used to wrap the mod's own call.
// Since v0.131.0 the cue calls the leaf sound player, sub_0046B2A0, and never
// this routine, so there is no path by which the mod can land back in its own
// hook and no flag needed to catch one.
static bool LadderStepSigMatches(uintptr_t addr)
{
    bool ok = false;
    __try {
        const unsigned char* p = (const unsigned char*)addr;
        ok = true;
        for (int i = 0; i < LADDER_STEP_SIG_LEN; i++) {
            if (p[i] != LADDER_STEP_SIG[i]) { ok = false; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// v0.131.0: the cue calls the SOUND PLAYER, not the engine's step wrapper, and
// touches no entity state at all. See ladder_cue_model.inl for why: the wrapper
// needed the player's movement mode set to 3, and writing that from the mod's
// own thread let the game catch it half-done and walk the player to the last
// ladder destination -- which is how Aaron finished a descent stranded on the
// ascent's two-triangle mid-ladder island with nowhere to walk.
typedef void (__cdecl* LadderPlaySoundFn)(int id, int selector, int pan, int volume);

static LadderPlaySoundFn s_ladderPlaySound   = nullptr;
static bool              s_ladderPlayChecked = false;

static bool LadderSoundSigMatches(uintptr_t addr)
{
    bool ok = false;
    __try {
        const unsigned char* p = (const unsigned char*)addr;
        ok = true;
        for (int i = 0; i < LADDER_SOUND_SIG_LEN; i++) {
            if (p[i] != LADDER_SOUND_SIG[i]) { ok = false; break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static LadderPlaySoundFn LadderPlaySound()
{
    if (s_ladderPlayChecked) return s_ladderPlaySound;
    s_ladderPlayChecked = true;
    if (LadderSoundSigMatches((uintptr_t)LADDER_SOUND_PLAY_ADDR))
        s_ladderPlaySound = (LadderPlaySoundFn)(uintptr_t)LADDER_SOUND_PLAY_ADDR;
    return s_ladderPlaySound;
}

// SEH-guarded, because a fault must not take the game down over a sound cue.
static void LadderStepInvoke(LadderPlaySoundFn fn)
{
    __try {
        fn(LADDER_SOUND_ID, LADDER_SOUND_SELECTOR, LADDER_SOUND_PAN, LADDER_SOUND_VOL);
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// v0.125.0: the engine just played a step for the climbing player. Returns the
// gap in ms when it was usable as a cadence sample, or 0, so the caller can log
// what was measured -- the whole point of this build is that the number is
// evidence rather than a guess, and evidence belongs in the log.
// v0.127.0: the raw gap and what it folded to, kept for the log line. A gap the
// engine skipped a stride on shows up here as "1359 -> 453 (x3)", which is the
// difference between a slower ladder and a missed one -- and the whole reason
// the cadence stopped drifting to 970 ms.
static unsigned s_ladderLastRawGapMs = 0;
static unsigned s_ladderLastFoldN    = 0;

static unsigned LadderNoteEngineStep(unsigned nowMs)
{
    unsigned folded = 0;
    s_ladderLastRawGapMs = 0;
    s_ladderLastFoldN    = 0;
    if (s_ladderEngineEver) {
        const unsigned gap = (unsigned)(nowMs - s_ladderEngineLastMs);
        s_ladderLastRawGapMs = gap;
        folded = LadderFoldGap(gap, s_ladderIntervalMs);
        if (folded != 0) {
            s_ladderLastFoldN  = (folded > 0) ? ((gap + folded / 2u) / folded) : 1u;
            s_ladderIntervalMs = LadderLearnInterval(s_ladderIntervalMs, gap);
            s_ladderLearned    = true;
        }
    }
    s_ladderEngineLastMs = (DWORD)nowMs;
    s_ladderEngineEver   = true;
    return folded;
}

// Called the frame a climb begins. Everything scoped to one ladder resets here:
// the step spacing (so the next climb steps on its own schedule rather than
// waiting out an interval that started minutes ago) and the engine's activity
// (so this ladder is judged on whether IT makes noise).
static void LadderClimbBegin(unsigned nowMs)
{
    s_ladderClimbStartMs = (DWORD)nowMs;
    s_ladderStepEver     = false;
    s_ladderStepIndex    = 0;
    s_ladderEngineEver   = false;
}

// Called every frame with the player's entity block and its movement mode.
// Returns true when a step was played, so the caller can log it.
// True while a scripted ladder move is running that sets no movement mode at
// all -- crroof1's descent, which animates in place and teleports. v0.128.0
// armed this from the engine's control-lock byte and v0.129.0 from the use
// button; both inferred, and both were wrong. Since v0.130.0 it is armed by the
// script's own PREQEW wait, gated on the player standing on a line the field
// calls a ladder.
static bool s_ladderScriptCue = false;

// The two ways the mod knows a ladder is being used: the engine's own movement
// mode, and the script's PREQEW wait for the descents that set no mode at all.
// One state, so the two can never both cue the same step.
static bool LadderIsClimbing(int mode)
{
    return LadderModeIsClimb(mode) || s_ladderScriptCue;
}

static bool LadderStepPoll(int entityPtr, int mode, unsigned nowMs)
{
    const bool climbing = LadderIsClimbing(mode);
    if (!climbing) { s_ladderStepEver = false; s_ladderStepIndex = 0; return false; }

    // The game is sounding this ladder itself -- v0.124.0 would have doubled it.
    if (LadderEngineIsStepping(s_ladderEngineEver, nowMs,
                               (unsigned)s_ladderEngineLastMs, s_ladderIntervalMs))
        return false;

    if (!LadderStepDue(climbing, nowMs, (unsigned)s_ladderClimbStartMs,
                       (unsigned)s_ladderStepLastMs, s_ladderStepEver, s_ladderIntervalMs))
        return false;
    LadderPlaySoundFn fn = LadderPlaySound();
    if (fn == nullptr) return false;
    (void)entityPtr;                     // v0.131.0: the cue no longer touches it
    s_ladderStepLastMs = (DWORD)nowMs;
    s_ladderStepEver   = true;
    s_ladderStepIndex++;
    LadderStepInvoke(fn);
    return true;
}
