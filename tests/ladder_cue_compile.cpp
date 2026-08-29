// ladder_cue_compile.cpp -- host probe for the ladder climb sound (#centra, v0.124.0).
//
// WHY THIS EXISTS. The climb sound lives in field_navigation.cpp's include
// chain, and that translation unit cannot be syntax-checked on the host -- it
// pulls in DirectInput and inline assembly, the same standing limitation
// src/world_map.cpp has carried for years. So the sound was split into its own
// includable unit with one dependency the caller keeps to itself (which entity
// is climbing, and what its movement mode says), and this probe builds the
// rest: the signature check, the resolve-once, the per-frame step spacing and
// the alternating feet.
//
// It matters more here than in most places, because the thing being built calls
// a HARDCODED ENGINE ADDRESS. The signature check is the only thing standing
// between a repacked exe and a jump into the middle of something else, and it
// is checked here against both the real prologue and a corrupted one.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>   // host only: a fake entity block the mod can address as u32

namespace Log { static void Field(const char*, ...) {} }

#include "ladder_cue_model.inl"
#include "ladder_cue.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

// A stand-in for the engine routine, plus a copy of its prologue so the
// signature check has something real to match.
static int g_calls = 0, g_lastFoot = -1, g_lastVol = -1, g_lastPan = -1, g_lastEnt = 0;
static int g_modeSeenByRoutine = -1;   // what +0x23C read as INSIDE the call

// A REAL ENTITY BLOCK, NOT A MADE-UP POINTER.
//
// v0.126.0 made the cue write the entity's movement mode byte for the length of
// the call, and the probe kept passing 0x1234 as the entity -- a number, not
// memory. It could not have caught anything about the swap, and once the byte
// was actually dereferenced it could not even run. The block is mapped low so
// the `int entityPtr` the engine's routine takes round-trips on a 64-bit host
// exactly as it does in the 32-bit game.
static unsigned char* g_ent = nullptr;
static int EntPtr() { return (int)(uintptr_t)g_ent; }
// +0x23C, the engine's movement mode byte. v0.131.8 removed the model constant
// that used to name it -- nothing in the mod writes this byte any more -- but
// the probe still keeps a real one, because "the cue leaves it alone" is the
// assertion that stands between here and the stranding bug.
static unsigned char* EntMode() { return g_ent + 0x23C; }

// v0.131.0: the cue calls the SOUND PLAYER, not the engine's step wrapper, and
// hands it no entity at all -- so the stand-in records what a mixer would see.
static int g_lastId = -1, g_lastSel = -1;
static void __cdecl FakeStepSound(int id, int sel, int pan, int vol)
{
    g_calls++; g_lastId = id; g_lastSel = sel; g_lastPan = pan; g_lastVol = vol;
    g_modeSeenByRoutine = (g_ent != nullptr) ? (int)*EntMode() : -1;
}

int main()
{
    printf("ladder_cue.inl compiles\n");

    g_ent = (unsigned char*)mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (g_ent == MAP_FAILED) { printf("FAIL: no low mapping for the fake entity\n"); return 1; }
    memset(g_ent, 0, 0x1000);

    // THE MODES. Read out of the four ladder opcode handlers; 2, 3 and 4 are
    // ladder moves and nothing else is. Kills a mutant that treats every
    // non-zero mode as a climb, which would play a step for ordinary walking.
    CHECK(LadderModeIsClimb(2) && LadderModeIsClimb(3) && LadderModeIsClimb(4),
          "the three ladder movement modes are climbs");
    CHECK(!LadderModeIsClimb(0) && !LadderModeIsClimb(1) && !LadderModeIsClimb(5),
          "standing, walking and anything else are not");

    // THE SIGNATURE CHECK, against the real sixteen bytes and a corrupted copy.
    unsigned char good[32]; memcpy(good, LADDER_SOUND_SIG, LADDER_SOUND_SIG_LEN);
    unsigned char bad[32];  memcpy(bad,  LADDER_SOUND_SIG, LADDER_SOUND_SIG_LEN);
    bad[LADDER_SOUND_SIG_LEN - 1] ^= 0xFF;
    CHECK(LadderSoundSigMatches((uintptr_t)good), "the real prologue matches");
    CHECK(!LadderSoundSigMatches((uintptr_t)bad),
          "a prologue that differs in its LAST byte does not");
    bad[0] ^= 0xFF;
    CHECK(!LadderSoundSigMatches((uintptr_t)bad), "nor one that differs in its first");
    // And the step wrapper's own signature still guards the measurement hook.
    unsigned char stepSig[32]; memcpy(stepSig, LADDER_STEP_SIG, LADDER_STEP_SIG_LEN);
    CHECK(LadderStepSigMatches((uintptr_t)stepSig),
          "the step routine's prologue still matches -- it is hooked, not called");
    // A null or unmapped address is caught by the SEH guard in the shipped
    // build. It is NOT asserted here: the host shim compiles __try away, so the
    // only thing this probe could prove is that Linux segfaults, which it does.

    // THE SPACING. The mount animation, then one step per interval.
    const unsigned IV = LADDER_STEP_MS_DEFAULT;
    CHECK(!LadderStepDue(true, 1000, 1000, 0, false, IV), "the first step waits out the mount");
    CHECK(LadderStepDue(true, 1000 + LADDER_STEP_GRACE_MS, 1000, 0, false, IV),
          "then plays");
    CHECK(!LadderStepDue(true, 1100, 1000, 1000, true, IV), "the next frame does not");
    CHECK(!LadderStepDue(true, 1000 + IV - 1, 1000, 1000, true, IV), "nor just inside the interval");
    CHECK(LadderStepDue(true, 1000 + IV, 1000, 1000, true, IV), "the next rung does");
    CHECK(!LadderStepDue(false, 99999, 1000, 0, false, IV), "and nothing plays when not climbing");
    // "Never stepped" is a flag, not a zero timestamp -- under a clock reading
    // 0 a zero sentinel means every step stamps 0 and the spacing never
    // engages, which is what the v0.123.0 probe caught in its first minute.
    CHECK(LadderStepDue(true, LADDER_STEP_GRACE_MS, 0, 0, false, IV),
          "a climb starting at tick zero still steps");
    CHECK(!LadderStepDue(true, 0, 0, 0, true, IV), "and still spaces the ones after it");

    // THE POLL, driven end to end against the stand-in.
    s_ladderPlaySound = FakeStepSound;   // stand in for the resolve
    s_ladderPlayChecked = true;
    s_ladderStepEver = false; s_ladderStepIndex = 0; s_ladderStepLastMs = 0;
    g_calls = 0;

    CHECK(!LadderStepPoll(EntPtr(), 0, 5000), "no step while standing");
    LadderClimbBegin(5000);
    CHECK(!LadderStepPoll(EntPtr(), 3, 5000), "nothing on the frame the climb starts");
    CHECK(LadderStepPoll(EntPtr(), 3, 5000 + LADDER_STEP_GRACE_MS),
          "a step once the mount animation is out of the way");
    CHECK(g_calls == 1, "exactly one");
    CHECK(g_lastId == LADDER_SOUND_ID, "the game's own ladder step sound id");
    CHECK(g_lastSel == LADDER_SOUND_SELECTOR, "on the selector the ladder branch passes");
    CHECK(g_lastVol == LADDER_SOUND_VOL && g_lastPan == LADDER_SOUND_PAN,
          "at centre pan and no louder than the engine's own loudest footstep");
    CHECK(LADDER_SOUND_VOL <= 0x7F - 8,
          "which is 0x7F minus the engine's minimum distance attenuation");
    CHECK(!LadderStepPoll(EntPtr(), 3, 5000 + LADDER_STEP_GRACE_MS + 10),
          "not again on the next frame");
    CHECK(LadderStepPoll(EntPtr(), 3, 5000 + LADDER_STEP_GRACE_MS + IV), "again a rung later");

    // ENDING THE CLIMB RESETS IT, so the next climb starts on its own schedule
    // instead of waiting out an interval that began minutes ago.
    CHECK(!LadderStepPoll(EntPtr(), 0, 6000), "the climb ends");
    LadderClimbBegin(6001);
    CHECK(LadderStepPoll(EntPtr(), 2, 6001 + LADDER_STEP_GRACE_MS),
          "and the next one steps on its own grace period, not the old interval");

    // ================================================================
    // v0.125.0: THE ENGINE'S OWN STEPS, MEASURED AND DEFERRED TO.
    // ================================================================
    // The hook feeds LadderNoteEngineStep. Two things follow: the cadence is
    // learned from the gaps, and the mod's own cue goes quiet while the game is
    // sounding the ladder itself -- which is v0.124.0's unnoticed doubling on
    // crtower3's and crtower1's ladders.
    s_ladderIntervalMs = LADDER_STEP_MS_DEFAULT;   // 469, the measured pace
    s_ladderLearned = false;
    LadderClimbBegin(20000);
    CHECK(LadderNoteEngineStep(20000) == 0, "the first engine step has no gap to measure");
    CHECK(LadderNoteEngineStep(20500) == 500, "the second one does");
    CHECK(s_ladderIntervalMs == 479, "and it moves the pace a third of the way");
    CHECK(s_ladderLearned, "which is now a measurement on top of a measurement");
    CHECK(LadderNoteEngineStep(20508) == 0,
          "two feet crossing on one frame is discarded, not learned from");
    CHECK(s_ladderIntervalMs == 479, "leaving the cadence where the real sample put it");

    // v0.127.0: A SKIPPED STRIDE FOLDS INSTEAD OF INFLATING. This is the whole
    // regression -- 953, 1359 and 1344 ms gaps walked v0.126.0's mean to 970 ms
    // and handed 586 ms to the silent roof ladder.
    {
        const unsigned before = s_ladderIntervalMs;
        CHECK(LadderNoteEngineStep(20508 + 958) == 479,
              "a doubled gap is reported as the stride it doubled");
        CHECK(s_ladderLastRawGapMs == 958, "with the raw gap kept for the log");
        CHECK(s_ladderLastFoldN == 2, "and the factor it folded by");
        CHECK(s_ladderIntervalMs <= before + 5,
              "so it barely moves the pace instead of inflating it");
    }

    // AND THE MOD SAYS NOTHING WHILE THAT IS HAPPENING.
    unsigned engineAt = 20508 + 958;
    g_calls = 0;
    CHECK(!LadderStepPoll(EntPtr(), 4, engineAt + 200),
          "the game is sounding this ladder; the mod is not");
    CHECK(g_calls == 0, "no doubled step");
    // v0.127.0: and it keeps saying nothing across the longest hole the engine
    // has ever been seen to leave -- 1359 ms on crtower3 at 16:51:22, which at
    // v0.125.0's two-interval slack was enough for the mod to cut in.
    CHECK(!LadderStepPoll(EntPtr(), 4, engineAt + 1359),
          "a 1359 ms hole in the engine's own rhythm is not the mod's to fill");
    CHECK(g_calls == 0, "still nothing");
    // Four intervals of real silence and the ladder is the mod's again.
    CHECK(LadderStepPoll(EntPtr(), 4, engineAt + s_ladderIntervalMs * 4 + 1),
          "a ladder the engine falls silent on is handed back");
    CHECK(g_calls == 1, "and sounded once");

    // A CLIMB IS JUDGED ON ITSELF. Steps the engine played on the previous
    // ladder must not silence the next, silent one -- the roof ladder follows
    // the tower ladders on the only route up, so this is the actual case.
    const unsigned learned = s_ladderIntervalMs;
    LadderClimbBegin(30000);
    g_calls = 0;
    CHECK(LadderStepPoll(EntPtr(), 3, 30000 + LADDER_STEP_GRACE_MS),
          "the silent roof ladder is sounded even though the last one was not");
    CHECK(g_calls == 1, "once");
    CHECK(s_ladderIntervalMs == learned,
          "at the cadence measured from the ladders that came before it");
    CHECK(learned >= 400 && learned <= 540,
          "which is the pace the game actually climbs at, not 586");

    // ================================================================
    // v0.131.0: THE CUE MUST NOT TOUCH THE ENTITY AT ALL.
    // ================================================================
    // v0.126.0 wrote the player's movement mode to 3 for the length of the call
    // so the engine's step wrapper would take its ladder branch. That runs on
    // AccessibilityThread while the game updates entities on its own, and the
    // game caught it: Aaron finished a descent standing on the ASCENT's
    // two-triangle mid-ladder island at (661,-622,19296) -- the ladder
    // destination still sitting in the entity block -- with nowhere to walk.
    // The cue now calls the sound player directly and writes nothing.
    *EntMode() = 0;
    const unsigned char before0 = *EntMode();
    s_ladderScriptCue = true;
    LadderClimbBegin(50000);
    g_calls = 0; g_modeSeenByRoutine = -1;
    CHECK(LadderStepPoll(EntPtr(), 0, 50000 + LADDER_STEP_GRACE_MS),
          "a descent with no movement mode steps");
    CHECK(g_calls == 1, "once");
    CHECK(*EntMode() == before0, "and the movement mode byte is untouched");
    CHECK(g_modeSeenByRoutine == 0,
          "the sound player never sees a borrowed mode, because none is written");

    // AN ASCENT IS THE SAME CALL. There is no longer a mode the cue cares about
    // -- a mutant that reintroduced the swap for "modes that do not sound as a
    // ladder" would fail the descent check above and this one together.
    *EntMode() = 4;
    LadderClimbBegin(60000);
    g_calls = 0;
    CHECK(LadderStepPoll(EntPtr(), 4, 60000 + LADDER_STEP_GRACE_MS), "an ascent steps");
    CHECK(*EntMode() == 4, "with its own mode untouched");
    s_ladderScriptCue = false;

    // WITH NO RESOLVED ROUTINE, NOTHING IS CALLED AND NOTHING FAULTS -- the
    // signature check failing must be silent, not a crash.
    s_ladderPlaySound = nullptr;
    LadderClimbBegin(40000);
    const int before = g_calls;
    CHECK(!LadderStepPoll(EntPtr(), 3, 40000 + LADDER_STEP_GRACE_MS),
          "an unresolved routine plays nothing");
    CHECK(g_calls == before, "and calls nothing");

    printf(g_fail ? "ladder_cue_compile: FAILED %d\n" : "ladder_cue_compile: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
