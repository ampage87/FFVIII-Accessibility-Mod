// bahamut_light_test.cpp -- the Deep Sea Research Center light puzzle's pure
// model (#bahamut-light, v0.98.0).
//
// Compiles src/bahamut_light_model.inl standalone -- the same text the mod's
// own translation unit includes -- and asserts the properties the corridor
// depends on. Every assertion below was written against a mutant: the note on
// each one says which change it kills.
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "bahamut_light_model.inl"

using namespace BahamutLightModel;

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

// ---------------------------------------------------------------------------
// The cue
// ---------------------------------------------------------------------------
static void TestCue()
{
    // The plain reading of the puzzle byte.
    CHECK(BlCueFor(true, 1, 0) == BL_CUE_STOP, "light armed -> Stop");
    CHECK(BlCueFor(true, 0, 0) == BL_CUE_GO,   "light clear -> Go");

    // A byte we could not read is not a byte. Kills the mutant that treats a
    // failed read as zero and cheerfully calls "Go" into an armed corridor.
    CHECK(BlCueFor(false, 0, 0) == BL_CUE_NONE, "unreadable -> no cue (from 0)");
    CHECK(BlCueFor(false, 1, 0) == BL_CUE_NONE, "unreadable -> no cue (from 1)");

    // THE SAFE BYTE WINS, AND THE CASE THAT MATTERS IS THE CONTRADICTORY ONE.
    // Koe::touch leaves var[1024] at 1 behind it, and a Hojo pocket can then set
    // var[1028] without anything clearing var[1024] -- so (1, 1) is reachable in
    // the real corridor and it is SAFE. Kills the mutant that checks the light
    // byte first, which would pass every non-contradictory case.
    CHECK(BlCueFor(true, 1, 1) == BL_CUE_CLEAR, "safe pocket beats a stale armed byte");
    CHECK(BlCueFor(true, 0, 1) == BL_CUE_CLEAR, "safe pocket with the light clear");
    // Any non-zero disarms: the script tests `== 0`, not `!= 1`.
    CHECK(BlCueFor(true, 1, 2) == BL_CUE_CLEAR, "safe byte is a zero test, not a one test");

    // The words themselves are the deliverable; a swap is silent otherwise.
    CHECK(strcmp(BlCueText(BL_CUE_GO),   "Go")   == 0, "Go says Go");
    CHECK(strcmp(BlCueText(BL_CUE_STOP), "Stop") == 0, "Stop says Stop");
    CHECK(BlCueText(BL_CUE_NONE)[0] == '\0', "no cue says nothing");
    // THE SAFE-STRETCH LINE MUST NOT SOUND LIKE "PUZZLE COMPLETE". v0.98.0 said
    // "Clear. The light cannot reach you here." and Aaron read it exactly that
    // way -- "got to where it said I was clear but then nothing happened" -- and
    // stood still in front of a stuck dialog box. It has to tell him to keep going.
    CHECK(strstr(BlCueText(BL_CUE_CLEAR), "walking") != nullptr,
          "the safe-stretch cue tells the player to keep walking");
    CHECK(strstr(BlCueText(BL_CUE_CLEAR), "Clear") == nullptr,
          "and does not say Clear, which reads as puzzle-cleared");
}

// ---------------------------------------------------------------------------
// The edge
// ---------------------------------------------------------------------------
static void TestEdge()
{
    CHECK(BlCueChanged(BL_CUE_STOP, BL_CUE_GO),  "arm -> clear is announced");
    CHECK(BlCueChanged(BL_CUE_GO, BL_CUE_STOP),  "clear -> arm is announced");
    CHECK(BlCueChanged(BL_CUE_NONE, BL_CUE_GO),  "the first reading is announced");

    // Repeating a cue into a 35-frame window is worse than silence.
    CHECK(!BlCueChanged(BL_CUE_GO, BL_CUE_GO),     "an unchanged Go is not repeated");
    CHECK(!BlCueChanged(BL_CUE_STOP, BL_CUE_STOP), "an unchanged Stop is not repeated");

    // Losing the read must not produce speech. Kills the mutant that drops the
    // `now != BL_CUE_NONE` term, which every other case above still passes.
    CHECK(!BlCueChanged(BL_CUE_GO, BL_CUE_NONE),   "a lost reading is not announced");
    CHECK(!BlCueChanged(BL_CUE_NONE, BL_CUE_NONE), "no reading at all is not announced");
}

// ---------------------------------------------------------------------------
// Getting the word out
// ---------------------------------------------------------------------------
static void TestSpeak()
{
    // Nothing on the wire: just say it.
    CHECK(BlSpeakDecision(false, false, 0,    BL_OWN_MS) == BL_SPEAK_QUEUE,
          "idle channel -> speak");
    CHECK(BlSpeakDecision(false, true, 99999, BL_OWN_MS) == BL_SPEAK_QUEUE,
          "idle channel -> speak even if our last cue is ancient");

    // Our own previous cue is the one thing worth cutting off.
    CHECK(BlSpeakDecision(true, true, 100, BL_OWN_MS) == BL_SPEAK_INTERRUPT,
          "our own recent cue is interrupted");

    // A story line is not. Bahamut's own voice comes down this corridor via
    // Koe::touch, and the next edge repeats the cue within seconds.
    CHECK(BlSpeakDecision(true, false, 0, BL_OWN_MS) == BL_SPEAK_DROP,
          "someone else speaking -> drop the cue");
    CHECK(BlSpeakDecision(true, true, BL_OWN_MS, BL_OWN_MS) == BL_SPEAK_DROP,
          "our claim on the channel expires at the boundary");
    CHECK(BlSpeakDecision(true, true, BL_OWN_MS + 1, BL_OWN_MS) == BL_SPEAK_DROP,
          "and stays expired past it");

    // The window has to be shorter than the phase it lives inside, or a cue
    // would still be claiming the channel when the next one arrives.
    CHECK(BL_OWN_MS < 3000u, "the ownership window is shorter than an armed phase");
}

// ---------------------------------------------------------------------------
// The skip
// ---------------------------------------------------------------------------
static void TestSkip()
{
    // The pair the Hojo lines write. Nothing to do once it is in place.
    CHECK(!BlSkipWriteNeeded(0, 1), "the disarmed pair needs no write");

    // Koe::touch takes both back: it sets var[1028] = 0 and var[1024] = 1.
    // Either half alone must re-trigger the hold. Kills a mutant that only
    // watches the light byte, and one that only watches the safe byte.
    CHECK(BlSkipWriteNeeded(1, 1), "a re-armed light byte is rewritten");
    CHECK(BlSkipWriteNeeded(0, 0), "a cleared safe byte is rewritten");
    CHECK(BlSkipWriteNeeded(1, 0), "Koe::touch taking both back is rewritten");

    // The skip is exactly the game's own disarm, so the value written to the
    // safe byte is 1 -- the literal the three Hojo::touch lines push.
    CHECK(!BlSkipWriteNeeded(0, 1) && BlSkipWriteNeeded(0, 2),
          "the hold targets 1 specifically, not just non-zero");
}

// ---------------------------------------------------------------------------
// When to prompt
// ---------------------------------------------------------------------------
static void TestPrompt()
{
    const unsigned SETTLE = 6000u;

    CHECK(!BlPromptArmed(false, false, true, true, 99999u, SETTLE), "not in the field, no prompt");
    CHECK(!BlPromptArmed(true, true, true, true, 99999u, SETTLE), "already answered, no prompt");

    // The normal path: the pulse proved itself, so the Director is past its
    // arrival cutscene whatever the story byte says.
    CHECK(BlPromptArmed(true, false, true, false, 0u, SETTLE),
          "a seen pulse arms the prompt immediately");

    // The fallback, for a player who walks into a Hojo pocket before the first
    // pulse and so never gives us an edge.
    CHECK(BlPromptArmed(true, false, false, true, SETTLE, SETTLE),
          "settled with the cutscene idle arms the prompt");
    CHECK(!BlPromptArmed(true, false, false, true, SETTLE - 1u, SETTLE),
          "the settle delay is honoured");

    // AND THE CUTSCENE IS NOT INTERRUPTED. var[614] == 1 is the Director's own
    // gate on the arrival scene; the prompt must wait it out no matter how long
    // the party has been standing there.
    CHECK(!BlPromptArmed(true, false, false, false, 99999u, SETTLE),
          "the arrival cutscene is never interrupted");

    // A SPOKEN PROMPT CAN BE MISSED, SO IT REPEATS. v0.98.0's engine ASK left a
    // box on screen to come back to; a sentence leaves nothing.
    CHECK(BlPromptDue(true, 0, BL_PROMPT_MAX, false, 0u, BL_PROMPT_REPEAT_MS),
          "the first prompt is due immediately");
    CHECK(!BlPromptDue(true, 1, BL_PROMPT_MAX, true, BL_PROMPT_REPEAT_MS - 1u, BL_PROMPT_REPEAT_MS),
          "a repeat waits out the interval");
    CHECK(BlPromptDue(true, 1, BL_PROMPT_MAX, true, BL_PROMPT_REPEAT_MS, BL_PROMPT_REPEAT_MS),
          "and is due when the interval elapses");
    CHECK(!BlPromptDue(true, BL_PROMPT_MAX, BL_PROMPT_MAX, true, 99999u, BL_PROMPT_REPEAT_MS),
          "the repeats are bounded");
    CHECK(!BlPromptDue(false, 0, BL_PROMPT_MAX, false, 99999u, BL_PROMPT_REPEAT_MS),
          "an unarmed prompt is never due");
    CHECK(BL_PROMPT_MAX > 1, "there is more than one chance to hear it");
}

// ---------------------------------------------------------------------------
// The answer
// ---------------------------------------------------------------------------
static void TestAnswer()
{
    CHECK(BlKeyChoice(true, false) == BL_MODE_GUIDE, "the guide key chooses Guide");
    CHECK(BlKeyChoice(false, true) == BL_MODE_SKIP,  "the skip key chooses Skip");
    CHECK(BlKeyChoice(false, false) == BL_MODE_NONE, "no key chooses nothing");
    // Both down is ambiguous and the wrong guess is expensive -- Skip rewrites
    // two engine bytes. Kills the mutant that tests the keys in sequence and so
    // silently prefers whichever is checked first.
    CHECK(BlKeyChoice(true, true) == BL_MODE_NONE, "both keys down chooses nothing");

    // An unanswered prompt leaves the mod guiding: the mode that changes nothing
    // about the game.
    CHECK(BlModeOnTimeout() == BL_MODE_GUIDE, "a timed-out prompt guides");

    // AND THE PROMPT ITSELF MUST NOT GET THE PLAYER KILLED. The v0.98.0 ASK let
    // a pulse arm underneath it and the arrow keys that drove its cursor were
    // exactly what battlekun watches for; the encounter fired three seconds
    // before the answer committed.
    CHECK(BlHoldSafeWhilePrompting(true, false), "the light is held off while asking");
    CHECK(!BlHoldSafeWhilePrompting(true, true), "and released once answered");
    CHECK(!BlHoldSafeWhilePrompting(false, false), "and never written off the field");
}

// ---------------------------------------------------------------------------
// Retiming the script
// ---------------------------------------------------------------------------
//
// A fake code array standing in for the one at *(uint32_t**)0x01D9CF50, built
// from sdcore1's own file so the probe verifies against the same bytes the mod
// will meet.
static void BuildFakeScript(uint32_t* code, int n)
{
    for (int i = 0; i < n; i++) code[i] = 0xDEADBEEFu;
    for (int i = 0; i < BL_SIG_COUNT; i++)   code[BL_SIG[i].idx]   = BL_SIG[i].word;
    for (int i = 0; i < BL_PATCH_COUNT; i++) code[BL_PATCH[i].idx] = BL_PATCH[i].from;
}

static void TestScriptRetime()
{
    const int n = BlScriptMaxIndex() + 1;
    uint32_t code[1024];
    bool already = true;

    BuildFakeScript(code, n);
    CHECK(BlScriptMatches(code, n, &already), "sdcore1's own words are recognised");
    CHECK(!already, "and are recognised as NOT yet patched");

    // Re-entering the field without a reload finds the patch already in place.
    // Kills the mutant that treats a patched array as a mismatch and so gives up
    // on the retiming the second time through the corridor.
    for (int i = 0; i < BL_PATCH_COUNT; i++) code[BL_PATCH[i].idx] = BL_PATCH[i].to;
    CHECK(BlScriptMatches(code, n, &already), "an already-patched array still matches");
    CHECK(already, "and reports itself as already patched");

    // HALF-PATCHED IS NOT PATCHED. If a write failed partway, the array is
    // neither shape and the mod must not assume the tempo it wanted.
    BuildFakeScript(code, n);
    code[BL_PATCH[0].idx] = BL_PATCH[0].to;
    CHECK(!BlScriptMatches(code, n, &already), "a half-written array is rejected");

    // One wrong signature word anywhere is enough to refuse. A different release,
    // or another field loaded into the same array, must not be written to.
    for (int k = 0; k < BL_SIG_COUNT; k++) {
        BuildFakeScript(code, n);
        code[BL_SIG[k].idx] ^= 1u;
        CHECK(!BlScriptMatches(code, n, &already),
              "a single altered signature word refuses the patch");
    }
    // ...and the same for the words about to be written.
    for (int k = 0; k < BL_PATCH_COUNT; k++) {
        BuildFakeScript(code, n);
        code[BL_PATCH[k].idx] = 0x07000001u;
        CHECK(!BlScriptMatches(code, n, &already),
              "an unexpected value at a patch site refuses the patch");
    }

    // EVERY PATCH SITE MUST HOLD A KNOWN VALUE -- the original or ours, nothing
    // else. An array whose patch sites are uniformly some third thing is not a
    // retimed loop, however tidy it looks; kills the mutant that counts any
    // unrecognised word as "already patched" and so skips the write entirely.
    BuildFakeScript(code, n);
    for (int i = 0; i < BL_PATCH_COUNT; i++) code[BL_PATCH[i].idx] = BlPshn(1);
    CHECK(!BlScriptMatches(code, n, &already),
          "patch sites holding a third value are refused, not called already-patched");

    // ---- FINDING it, because the index base is not ours --------------------
    //
    // The v0.103.0 BAT read `RET 8`, `EXT_DISPATCH` and `PSHN_L 30` at 637 / 645
    // / 656 -- sdcore1's code[854], code[862] and code[873]. The array behind the
    // engine's pointer is offset from this file's indices, so the loop has to be
    // found rather than assumed.
    {
        const int W = 600;
        static uint32_t win[1600];
        for (int i = 0; i < 1600; i++) win[i] = 0xFFFFFFFFu;
        // Plant the loop at an arbitrary offset, the way the real array does.
        uint32_t fake[1024];
        BuildFakeScript(fake, n);
        for (int i = 0; i < n; i++) win[W + i] = fake[i];

        int base = -1; bool ap = true;
        CHECK(BlScriptScan(win, 1600, &base, &ap) == 1, "the loop is found exactly once");
        CHECK(base == W, "and at the offset it was planted at");
        CHECK(!ap, "and reported as not yet patched");

        // Found again after it has been written -- a second visit to the corridor.
        for (int i = 0; i < BL_PATCH_COUNT; i++) win[W + BL_PATCH[i].idx] = BL_PATCH[i].to;
        CHECK(BlScriptScan(win, 1600, &base, &ap) == 1, "an already-retimed loop is still found");
        CHECK(ap, "and reported as already patched");

        // A window with no loop in it writes nothing. Kills the mutant that
        // returns a base when nothing matched.
        for (int i = 0; i < 1600; i++) win[i] = 0xFFFFFFFFu;
        CHECK(BlScriptScan(win, 1600, &base, &ap) == 0, "an empty window finds nothing");
        CHECK(base == -1, "and reports no base");

        // TWO MATCHES MEANS WRITE NOTHING. If the pattern were ever less specific
        // than it looks, guessing which copy is the live one is exactly the wrong
        // move. Kills the mutant that stops at the first hit.
        for (int i = 0; i < 1600; i++) win[i] = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++) { win[100 + i] = fake[i]; win[800 + i] = fake[i]; }
        CHECK(BlScriptScan(win, 1600, &base, &ap) == 2, "two copies are both counted");
        CHECK(base == -1, "and neither is chosen");

        CHECK(BlScriptScan(nullptr, 1600, &base, &ap) == 0, "a null window finds nothing");
    }

    // ---- and re-applying it, because a battle takes it away ----------------
    //
    // The v0.104.0 BAT retimed the loop, ran four clean cycles at 3,562 / 3,032
    // ms, took a random encounter, and came back to `Stop held 5,297 ms` -- the
    // script's own numbers. The field is reloaded from the archive on the way
    // back from a battle, and an encounter is the thing this puzzle is made of.
    CHECK(BlPatchWorthLogging((int)BLP_WROTE, false), "the first retiming is logged");
    CHECK(BlPatchWorthLogging((int)BLP_WROTE, true),
          "and so is every re-application -- how often a battle costs it is the "
          "reason the re-check exists");
    CHECK(BlPatchWorthLogging((int)BLP_FAIL, true), "losing it is logged");
    // The steady state is silent. A line a second saying nothing changed would
    // bury the ones that matter.
    CHECK(!BlPatchWorthLogging((int)BLP_ALREADY, true), "an unchanged retiming is not logged");
    CHECK(!BlPatchWorthLogging((int)BLP_ALREADY, false), "nor is one that was never there");
    CHECK(!BlPatchWorthLogging((int)BLP_FAIL, false),
          "and a field where the loop was never found stays quiet after the first miss");

    // Often enough to catch a reload before the player walks into the old tempo,
    // rarely enough that the window search is not running every frame.
    CHECK(BL_SCRIPT_RECHECK_MS >= 250u && BL_SCRIPT_RECHECK_MS <= 2000u,
          "the re-check interval is between a quarter-second and two seconds");
    CHECK(BL_SCRIPT_RECHECK_MS < BL_TEMPO_SAFE_MS,
          "and is shorter than a safe window, so a reload cannot outlast one");

    // Never read past the end of the array, and never accept a null one.
    BuildFakeScript(code, n);
    CHECK(!BlScriptMatches(code, BlScriptMaxIndex(), &already),
          "an array one word too short is refused");
    CHECK(!BlScriptMatches(nullptr, n, &already), "a null array is refused");

    // The retimed cycle has to reproduce the tempo the metronome established,
    // or the two paths would tell the player different things.
    CHECK((unsigned)BlRtSafeFrames()  * BL_FRAME_MS == BL_TEMPO_SAFE_MS,
          "the retimed safe window matches the metronome's");
    CHECK((unsigned)BlRtArmedFrames() * BL_FRAME_MS == BL_TEMPO_ARMED_MS,
          "and so does the armed window");

    // THE POINT OF THE WHOLE EXERCISE: bright means armed, dark means safe. The
    // arm must land exactly where the 60-frame fade-in finishes, and the disarm
    // where the fade-out does, or a low-vision player is back to two stories.
    CHECK(BL_RT_FADE == 60, "both of the script's fades are 60 frames");
    for (int i = 0; i < BL_PATCH_COUNT; i++) {
        if (BL_PATCH[i].idx == 637 || BL_PATCH[i].idx == 670)
            CHECK(BL_PATCH[i].to == BlPshn(BL_RT_FADE),
                  "the pre-arm wait is exactly the fade-in");
        if (BL_PATCH[i].idx == 699)
            CHECK(BL_PATCH[i].to == BlPshn(BL_RT_FADE - BL_RT_C_SPLIT),
                  "bucket C's split pre-arm still totals the fade-in");
        if (BL_PATCH[i].idx == 650 || BL_PATCH[i].idx == 683 || BL_PATCH[i].idx == 714)
            CHECK(BL_PATCH[i].to == BlPshn(BL_RT_FADE),
                  "the armed fade-out wait is exactly the fade-out");
    }

    // NO WAIT IS EVER SET TO ZERO -- bucket C's pre-arm is split in two and only
    // the first half is moved, which is the whole reason it is 41 and not 60.
    for (int i = 0; i < BL_PATCH_COUNT; i++)
        CHECK((BL_PATCH[i].to & 0x00FFFFFFu) > 0u, "no wait is retimed to zero");

    // All three buckets end up at the same tempo, so the safe window stops being
    // the earliest of three and becomes a number the cue can be exact about.
    int tails = 0;
    for (int i = 0; i < BL_PATCH_COUNT; i++)
        if (BL_PATCH[i].idx == 656 || BL_PATCH[i].idx == 689 || BL_PATCH[i].idx == 720) {
            CHECK(BL_PATCH[i].to == BlPshn(BL_RT_TAIL), "every bucket gets the same tail");
            tails++;
        }
    CHECK(tails == 3, "all three buckets are retimed, not just the one");

    // Every patch word is a PSHN_L. Writing anything else into the code array
    // would be rewriting the loop's shape, not its tempo.
    for (int i = 0; i < BL_PATCH_COUNT; i++) {
        CHECK((BL_PATCH[i].to   >> 24) == 0x07u, "a patched word is still a PSHN_L");
        CHECK((BL_PATCH[i].from >> 24) == 0x07u, "and so is the word it replaces");
    }

    // With the script retimed the warning can be generous, because the window is
    // known rather than the shortest of three.
    CHECK(BlWarnDelayMs(BlRtSafeFrames(), BL_TEMPO_LEAD_MS) > BlStopWarnDelayMs(),
          "the retimed path gives the player more of the window than the guess did");
    CHECK(BlWarnDelayMs(BlRtSafeFrames(), BL_TEMPO_LEAD_MS)
              + BL_TEMPO_LEAD_MS == (unsigned)BlRtSafeFrames() * BL_FRAME_MS,
          "and still speaks one full lead before the arm");
}

// ---------------------------------------------------------------------------
// The metronome
// ---------------------------------------------------------------------------
static void TestTempo()
{
    // The byte IS the phase, as far as battlekun1/battlekun4 are concerned.
    CHECK(BlTempoLightVar(BL_TEMPO_ARMED) == 1, "the armed phase sets the byte");
    CHECK(BlTempoLightVar(BL_TEMPO_SAFE)  == 0, "the safe phase clears it");
    // OFF must read as SAFE, not as armed: a metronome that stopped (the write
    // failed, or Skip took over) must never leave the corridor lethal.
    CHECK(BlTempoLightVar(BL_TEMPO_OFF)   == 0, "a stopped metronome leaves the byte clear");

    // While the mod owns var[1024], the Director has to be kept out of it --
    // otherwise it writes 1 on its own schedule and there is a script frame per
    // cycle where battlekun can read it before the mod's next poll.
    CHECK(BlTempoHoldsDirectorOut(BL_TEMPO_SAFE),  "the Director is held out during safe");
    CHECK(BlTempoHoldsDirectorOut(BL_TEMPO_ARMED), "and during armed");
    CHECK(!BlTempoHoldsDirectorOut(BL_TEMPO_OFF),  "and released when the metronome stops");

    // The warning is a deadline the mod set itself.
    const unsigned at = BL_TEMPO_SAFE_MS - BL_TEMPO_LEAD_MS;
    CHECK(!BlTempoWarnDue(BL_TEMPO_SAFE, false, at - 1u, BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS),
          "the warning waits for its moment");
    CHECK(BlTempoWarnDue(BL_TEMPO_SAFE, false, at, BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS),
          "and fires one lead before the safe phase ends");
    CHECK(!BlTempoWarnDue(BL_TEMPO_SAFE, true, 99999u, BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS),
          "and only once per phase");
    // Kills the mutant that drops the phase test and warns all through the armed
    // phase too -- every case above still passes without it.
    CHECK(!BlTempoWarnDue(BL_TEMPO_ARMED, false, 99999u, BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS),
          "and never during the armed phase");
    CHECK(!BlTempoWarnDue(BL_TEMPO_OFF, false, 99999u, BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS),
          "and never when the metronome is off");

    // The phases end on their own lengths, and NOT on each other's. Kills the
    // mutant that uses safeMs for both, which a symmetric pair of numbers would
    // hide entirely.
    CHECK(BL_TEMPO_SAFE_MS != BL_TEMPO_ARMED_MS,
          "the two phase lengths differ, so a test can tell them apart");
    CHECK(!BlTempoPhaseOver(BL_TEMPO_SAFE, BL_TEMPO_SAFE_MS - 1u, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS),
          "safe runs its full length");
    CHECK(BlTempoPhaseOver(BL_TEMPO_SAFE, BL_TEMPO_SAFE_MS, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS),
          "and then ends");
    CHECK(!BlTempoPhaseOver(BL_TEMPO_ARMED, BL_TEMPO_ARMED_MS - 1u, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS),
          "armed runs its full length");
    CHECK(BlTempoPhaseOver(BL_TEMPO_ARMED, BL_TEMPO_ARMED_MS, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS),
          "and then ends");
    CHECK(!BlTempoPhaseOver(BL_TEMPO_OFF, 99999u, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS),
          "an off metronome has no phase to end");

    // NEVER ARM OVER A SCENE. Koe's voice line and Bahamut's own take player
    // control with a window up; a key held from before one of those would become
    // an encounter nobody could have avoided.
    CHECK(!BlTempoMayArm(true),  "a scene blocks the arm");
    CHECK(BlTempoMayArm(false),  "and an empty screen allows it");

    // The safe phase has to hold a spoken cue, a reaction, some walking and the
    // lead. If the lead ever grew past the phase there would be no walking left.
    CHECK(BL_TEMPO_SAFE_MS > BL_TEMPO_LEAD_MS + 1000u,
          "the safe phase leaves real walking time after the lead");
    // And it has to beat what the game gives, or none of this was worth doing:
    // the script's longest safe window is 60 frames.
    CHECK(BL_TEMPO_SAFE_MS > (unsigned)BlSafeFrames(BL_F_TAIL_HI) * BL_FRAME_MS,
          "the metronome's safe phase is longer than the script's longest");
    // The lead has to beat the one the observation path could manage, which is
    // what made the old path unplayable.
    CHECK(BL_TEMPO_LEAD_MS > BL_STOP_LEAD_MS,
          "and the warning comes further ahead than the predicted path could give");
}

// ---------------------------------------------------------------------------
// The lead time on Stop
// ---------------------------------------------------------------------------
static void TestStopLead()
{
    // THE CUE MUST LAND BEFORE THE EARLIEST POSSIBLE ARM. v0.98.0 fired it ON the
    // arm edge, which is the same frame battlekun goes live -- "almost
    // synchronous with my getting attacked", and exactly synchronous in fact.
    const unsigned earliest = (unsigned)BL_SAFE_MIN_FRAMES * BL_FRAME_MS;
    CHECK(BlStopWarnDelayMs() < earliest, "the warning fires before the earliest arm");
    CHECK(earliest - BlStopWarnDelayMs() == BL_STOP_LEAD_MS,
          "and by exactly the lead the speech needs");

    // The lead has to be worth having and has to leave some walking in the
    // window. Kills a mutant that sets the lead to zero (no lead at all) and one
    // that sets it past the window (no walking at all).
    CHECK(BL_STOP_LEAD_MS >= 200u, "the lead is long enough for speech to start");
    CHECK(BlStopWarnDelayMs() > 0u, "and leaves the player some of the window");

    // The earliest arm is the 35-frame tail, not the 50 or the 60. Planning
    // against a longer one is planning to be late a third of the time.
    CHECK(BL_SAFE_MIN_FRAMES == BlSafeFrames(BL_F_TAIL_LO),
          "the deadline is the shortest safe window the script can pick");

    // The timer itself.
    CHECK(!BlStopWarnDue(true, false, BlStopWarnDelayMs() - 1u, BlStopWarnDelayMs()),
          "the warning waits for its delay");
    CHECK(BlStopWarnDue(true, false, BlStopWarnDelayMs(), BlStopWarnDelayMs()),
          "and fires when the delay elapses");
    CHECK(!BlStopWarnDue(true, true, 99999u, BlStopWarnDelayMs()),
          "and fires only once per window");
    CHECK(!BlStopWarnDue(false, false, 99999u, BlStopWarnDelayMs()),
          "and never outside a Go window");

    // THE ARM EDGE IS A BACKSTOP, NOT A REPEAT. Saying "Stop" again over a player
    // who stopped 800 ms ago is noise; saying nothing when the early warning
    // never fired -- a battle suspended the field script between Go and the arm
    // -- is silence at the one moment it matters.
    CHECK(!BlSpeakOnArm(true), "the arm does not repeat a warning already given");
    CHECK(BlSpeakOnArm(false), "but does speak when no warning was given");

    // A guard against the frame rate drifting out of the model: 158 frames at
    // BL_FRAME_MS has to reproduce the 5,297 ms the BAT measured.
    const unsigned armedMs = (unsigned)BlArmedFrames() * BL_FRAME_MS;
    CHECK(armedMs > 5000u && armedMs < 5600u,
          "the modelled armed phase matches the 5,297 ms measured");
}

// ---------------------------------------------------------------------------
// The windows, as the script defines them
// ---------------------------------------------------------------------------
static void TestWindows()
{
    // Straight from Director::default: 110 + 48 armed, and three safe tails.
    CHECK(BlArmedFrames() == 158, "the armed phase is 110 + 48 frames");
    CHECK(BlSafeFrames(BL_F_TAIL_LO)  == 35, "the shortest safe window is 35 frames");
    CHECK(BlSafeFrames(BL_F_TAIL_MID) == 50, "the middle safe window is 50 frames");
    CHECK(BlSafeFrames(BL_F_TAIL_HI)  == 60, "the longest safe window is 60 frames");

    // THE ASYMMETRY IS THE PUZZLE. The light is dangerous for well over twice
    // as long as it is safe, which is why the cue fires on the edge and is
    // never queued behind anything.
    CHECK(BlArmedFrames() > 2 * BlSafeFrames(BL_F_TAIL_HI),
          "armed is more than twice the longest safe window");

    // The brightening lead-in is safe: the script waits 18 frames after the
    // light starts to glow before it arms the byte. A model that folded those
    // frames into the armed phase would have the mod calling Stop early.
    CHECK(BL_F_BRIGHTEN == 18, "the brightening lead-in is 18 safe frames");
    CHECK(BlSafeFrames(0) == BL_F_BRIGHTEN + BL_F_DARK,
          "a zero tail still leaves the lead-in and the dark");
}

// ---------------------------------------------------------------------------
// The addresses
// ---------------------------------------------------------------------------
static void TestAddresses()
{
    // The two bytes battlekun1/battlekun4 and the Hojo lines actually use.
    CHECK(BL_VAR_BASE + (unsigned)BL_VAR_LIGHT == 0x01CFEDB8u, "var[1024] is 0x01CFEDB8");
    CHECK(BL_VAR_BASE + (unsigned)BL_VAR_SAFE  == 0x01CFEDBCu, "var[1028] is 0x01CFEDBC");
    CHECK(BL_VAR_BASE + (unsigned)BL_VAR_STORY == 0x01CFEC1Eu, "var[614] is 0x01CFEC1E");
    // The field id is the game's own: sdcore2 asks `var[84] == 846` to find out
    // whether the party arrived from this room.
    CHECK(BL_FIELD_ID == 846u, "the puzzle field is 846");
    CHECK(strcmp(BL_FIELD, "sdcore1") == 0, "the puzzle field is sdcore1");
}

int main()
{
    TestCue();
    TestEdge();
    TestSpeak();
    TestSkip();
    TestPrompt();
    TestAnswer();
    TestScriptRetime();
    TestTempo();
    TestStopLead();
    TestWindows();
    TestAddresses();
    if (g_fail == 0) printf("bahamut_light_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
