// ladder_cue_model.inl -- CONFIRMING THAT A CLIMB IS ACTUALLY HAPPENING
//
// v0.123.0 (#centra). Aaron, after the roof ladder finally worked: "that
// particular ladder on the roof for some reason does not have the ladder
// climbing sound that is normally played. Can we institute that sound effect
// for that ladder as well? It is useful for a blind player to confirm they are
// actually climbing."
//
// THE GAME IS NOT WITHHOLDING A SOUND -- IT IS RUNNING A DIFFERENT OPCODE.
// Field scripts move a character along a ladder with one of four opcodes, and
// all four do the same thing to the entity's movement mode byte at +0x23C:
//
//     0x024  handler 0x00525740  ->  mode 2
//     0x025  handler 0x00525900  ->  mode 3
//     0x026  handler 0x00525A30  ->  mode 3
//     0x027  handler 0x00525B60  ->  mode 4
//
// and the Centra Ruins uses three of them. crtower3's `laddw0` runs 0x024,
// crtower3's `ladup0` and crtower1's `p0_ladup0` run 0x027 -- and those are the
// ladders Aaron says make the noise. crroof1's `lad0`, the roof ladder, is the
// only one in the ruins that runs **0x026**, and it is the only one that is
// silent. The sound belongs to the movement mode, the mode belongs to the
// opcode, and the opcode is Square's choice in a script we do not patch.
//
// WHICH MEANS THE FIX IS NOT TO CHASE THEIR SOUND. Making the mod fire the
// engine's own SE would leave every OTHER ladder in the game that happens to
// use 0x025 or 0x026 just as silent, and it would tie an accessibility cue to
// an internal audio path that changes nothing about whether the player can hear
// it over the music. The mod plays its own cue on **every** ladder move
// instead, so "am I climbing?" has one answer everywhere rather than an answer
// that depends on which of four interchangeable opcodes a 1999 script author
// happened to type.
//
// NO DIRECTION IN THE CUE, and that is a decision rather than an omission. The
// obvious guess is that the opcodes come in up/down pairs -- an older comment in
// this project even names 0x025 LADDERUP and 0x026 LADDERDOWN. The disc says
// otherwise: crtower3's `ladup0` (up) runs 0x027 while its `laddw0` (down) runs
// 0x024, and crroof1's `lad0` -- which v0.116.0 established travels UP, from
// z=18403 to z=19496 -- runs the one supposedly called LADDERDOWN. The opcode
// does not encode the direction, so the cue does not claim to.

// v0.131.8: the four-opcode table above is EVIDENCE, not code. Builds up to
// v0.129.0 dispatched on the opcode; since v0.130.0 the cue is driven by the
// script's own PREQEW wait and by the movement mode the handler leaves behind,
// so nothing in the mod ever asks "which opcode was that". The table stays
// written down because it is how the silent ladder was found, and deleting the
// reasoning would cost the next reader the same week it cost this one -- but it
// is a comment now, so it cannot rot into a second source of truth.

// THE CUE IS THE GAME'S OWN STEP SOUND, not one of ours.
//
// The first cut of this build synthesised three knocks, on the reasoning that
// chasing the engine's audio path would be fragile. Aaron: "why can't we
// utilize the same climbing sound as the rest of the ladders in the game?" --
// and he was right, because that reasoning was an assumption I had not tested.
// The engine's own routine is one call:
//
//     sub_00520260(entityPtr, foot /*0|1*/, volume /*0x7F*/, pan /*0x80*/)
//
// -- cdecl, four arguments, four call sites in the exe (0x004783D6, 0x00478420,
// 0x0052A31C, 0x0052A35C), each cleaning with `add esp, 0x10` and passing
// volume 0x7F or 0x40 with pan 0x80 for centre.
//
// It picks the sound ITSELF from the entity's movement mode at +0x23C:
//
//     0x00520354  cmp dl, 3 / je  -> 0x005203BB
//     0x00520359  cmp dl, 4 / je  -> 0x005203BB
//     0x005203BB  push vol; push pan; push 0x400000; push 0x3F
//                 call 0x0046B2A0            <- the ladder step, sound id 0x3F
//
// with every other mode falling through to the terrain footstep table at
// 0x01CE4BFC. So the mod does not choose a sound, hardcode an id, or guess at a
// volume: it asks the engine to make the noise this character on this surface
// would make, and on a ladder that is the ladder sound the other ladders play.
//
// WHICH MEANS THE ROOF LADDER WAS NEVER MISSING THE SOUND -- IT WAS MISSING THE
// CALL. Mode 3 reaches the ladder branch exactly as mode 4 does. The routine is
// driven by footstep event frames in the climbing animation, and crroof1's
// `lad0` runs animation 3 (`0x031 3`) where crtower3's runs animation 6. One
// has the step frames and one does not. Nothing about the sound is unavailable
// to us; it simply was not being asked for.
//
// SO THE MOD ASKS, ON EVERY LADDER, for as long as the climb lasts. The
// movement mode is the engine's own statement that a ladder move is in
// progress, so polling it needs no timer to guess a duration: steps play while
// the mode is a ladder mode and stop the frame it clears.
// v0.126.0 (#centra): MODE 2 IS WHY THE WAY DOWN IS SILENT.
//
// Aaron: "I only heard it when going up, not when going down." The answer is
// already written at the top of this file and nobody had joined the two halves
// of it up. sub_00520260 picks its sound from the movement mode byte:
//
//     0x00520354  cmp dl, 3 / je  -> 0x005203BB   <- the ladder branch
//     0x00520359  cmp dl, 4 / je  -> 0x005203BB
//                 ...everything else falls through to the terrain footstep table
//
// **ONLY 3 AND 4 REACH THE LADDER SOUND.** And the opcode table says 0x024 sets
// mode 2 -- 0x024 being crtower3's `laddw0`, which is the ladder going DOWN.
// So on a descent the mod has been asking the engine for a sound all along and
// the engine has been answering with a footstep, because it was asked in a mode
// that is not a ladder as far as that routine is concerned. Ten calls, no ladder
// noise, exactly what Aaron heard.
//
// THE FIX DOES NOT HARDCODE THE SOUND ID, which is the whole reason this cue
// calls the engine instead of playing its own knock. It presents mode 3 for the
// length of the call and puts the byte back, so the routine takes the branch it
// would have taken on any ascent and still chooses the id, the volume and the
// surface itself. That also generalises past this one ladder: ANY mode a ladder
// opcode sets now sounds like a ladder, which is what "repurpose this for other
// silent ladders" actually needs -- mode 2 is not special, it is just the first
// one that turned up.
// **AND THAT FIX WAS WITHDRAWN IN v0.131.0 -- READ ON BEFORE BELIEVING IT.**
// Presenting mode 3 meant WRITING the player's entity from the mod's own
// thread, and the game caught it half-done and walked Aaron onto a landing he
// could not leave. The v0.131.0 block at the bottom of this file has the whole
// account. What survives from this one is the reading of the branch -- only
// modes 3 and 4 reach sound 0x3F -- which is where the cue gets the id it now
// plays directly. The constants that did the swapping are gone with it.

// v0.125.0 (#centra): THE CADENCE IS MEASURED, NOT CHOSEN.
//
// Aaron on v0.124.0: "did hear the ladder sounds now on that top ladder,
// however, the climbing sounds were very fast compared to the way they usually
// sound on a ladder." His log says exactly how fast: eighteen steps between
// `climb started` at 12:28:09 and `climb ended` at 12:28:14, which is the 260 ms
// this file used to hardcode. **260 WAS A GUESS**, written in the same build
// that had just finished arguing against guessing, and it is wrong by roughly
// the factor a listener would call "very fast".
//
// THE ENGINE ALREADY KNOWS THE ANSWER, so the mod stops guessing and asks it.
// sub_00520260 fires on a zero crossing: 0x004783A2 compares the previous foot
// offset in `si` against the new one in `bx` and plays a step when their signs
// differ, once per foot, twice per animation cycle. That means the true cadence
// is a property of the climbing animation's playback speed -- not a number
// anybody can read out of the exe, and not one worth guessing at twice. So this
// build hooks the routine, times the gaps between the engine's own steps on the
// ladders that already sound, and replays that interval on the ones that do not.
//
// AND IT FIXES SOMETHING AARON HAS NOT HIT YET. v0.124.0 polls the movement mode
// and steps on every ladder -- including the ones the engine is already sounding.
// On crtower3's `ladup0` and crtower1's `p0_ladup0` that is a SECOND set of steps
// laid over the game's own. The Centra run that produced the log climbed only the
// roof ladder, so the doubling never reached his ears; the hook that measures the
// cadence is also what detects the engine stepping and holds the mod's cue back.
// v0.126.0 (#centra): 469 IS MEASURED, NOT CHOSEN -- IT IS THE NUMBER THE
// HOOK CAME BACK WITH. The 2026-08-28 13:06 run timed fifteen consecutive
// engine steps on the crtower ladder: 468, 407, 468, 453, 469, 469, 469,
// 468, 469, 469, 469, 453, 469, 468. Eleven of the fourteen gaps are 468-469
// ms; the 453 and 407 outliers are one and three frames short at 64 Hz, which
// is the poll quantising, not the animation changing speed. So the climbing
// animation's true cadence is ~469 ms per rung and that is now the starting
// value, not 700. Aaron heard the running mean (465 ms) on crroof1's silent
// ladder and called it "the normal pace" -- this locks that pace in so a
// silent ladder sounds right on the FIRST climb, with no engine-sounded
// ladder needed ahead of it to teach the mod the rhythm. The learning below
// still runs and still refines; it just no longer has anything to rescue.
static const unsigned LADDER_STEP_MS_DEFAULT = 469;  // measured, see above
static const unsigned LADDER_STEP_MS_MIN     = 300;  // faster than this is not a climb
static const unsigned LADDER_STEP_MS_MAX     = 1400; // slower than this is a pause
// A climb has a mount animation before the first rung. Waiting this long before
// the mod's first step also gives an engine-sounded ladder time to declare
// itself, so the mod never lays a step over the game's own opening one.
static const unsigned LADDER_STEP_GRACE_MS   = 400;

static unsigned LadderClampInterval(unsigned ms)
{
    if (ms < LADDER_STEP_MS_MIN) return LADDER_STEP_MS_MIN;
    if (ms > LADDER_STEP_MS_MAX) return LADDER_STEP_MS_MAX;
    return ms;
}

// A gap only counts as a cadence sample if it could plausibly be one. Two steps
// 80 ms apart are the two feet crossing on the same frame; two steps four
// seconds apart span a pause, a cutscene, or two separate ladders.
static bool LadderGapIsSample(unsigned gapMs)
{
    return gapMs >= LADDER_STEP_MS_MIN && gapMs <= LADDER_STEP_MS_MAX;
}

// ============================================================================
// v0.127.0 (#centra): A GAP THE ENGINE SKIPPED IS NOT A SLOWER LADDER
// ============================================================================
// The 16:39-16:54 run measured nineteen gaps on the crtower ladder and the
// running mean walked 469 -> 783 -> 970 before settling at 586, which is the
// number crroof1's silent roof ladder then played at. The game's own rate is
// 468-469 ms (v0.126.0 timed eleven of fourteen gaps there). So the mod spent
// that climb a quarter slower than the ladder it was imitating.
//
// FOUR OF THE NINETEEN GAPS DID IT: 953, 1359, 1344 and 672 ms. The first
// three are 2x and 3x the real interval -- the engine's zero crossing missing
// a stride, not the animation slowing down -- and averaging them in is
// arithmetic that cannot be right, because a skipped step contains no
// information about the pace except that it was a whole number of steps.
//
// SO A GAP IS FOLDED BEFORE IT IS BELIEVED. Divide by 1, 2 or 3 and keep the
// quotient closest to the pace we already have, provided it lands inside a
// quarter of it. 953/2 = 476 and 1359/3 = 453 both survive and both are the
// truth; 672 does not divide into anything plausible (672 itself is too slow,
// 336 too fast) and is dropped rather than dragged into the mean. Replayed
// against the whole run this holds the cadence between 426 and 476 ms
// throughout -- the pace Aaron called normal -- instead of reaching 970.
//
// THE BAND IS 80-125% AND THAT IS NOT ARBITRARY. A gap around 1.5x the pace is
// genuinely ambiguous -- one slow stride or two fast ones, and nothing in the
// data says which -- so the band is set so that BOTH readings of it fall
// outside and it is dropped. 672 against a 452 ms belief is that case: 672 is
// too slow and its halving, 336, is too fast. Widen the band to +/-35% and the
// halving squeaks in, which is the exact failure the folding exists to prevent
// -- a fold that finds SOME quotient in range for any gap at all is a licence
// to believe anything.
static const unsigned LADDER_FOLD_MAX_N   = 3;   // 2x and 3x skips are what the log shows
static const unsigned LADDER_BAND_LO_PCT  = 80;
static const unsigned LADDER_BAND_HI_PCT  = 125;

static unsigned LadderAbsDiff(unsigned a, unsigned b)
{
    return (a > b) ? (a - b) : (b - a);
}

// The per-step interval this gap implies, or 0 when it implies nothing usable.
static unsigned LadderFoldGap(unsigned gapMs, unsigned intervalMs)
{
    if (intervalMs == 0) return 0;
    unsigned best = 0;
    for (unsigned n = 1; n <= LADDER_FOLD_MAX_N; n++) {
        const unsigned folded = gapMs / n;
        if (!LadderGapIsSample(folded)) continue;
        if (folded * 100u < intervalMs * LADDER_BAND_LO_PCT) continue;
        if (folded * 100u > intervalMs * LADDER_BAND_HI_PCT) continue;
        if (best == 0 || LadderAbsDiff(folded, intervalMs) < LadderAbsDiff(best, intervalMs))
            best = folded;
    }
    return best;
}

// Learned as a running mean weighted two-to-one toward what we already believe,
// so one odd gap cannot swing the cue.
//
// v0.127.0 drops v0.125.0's "the first real sample is taken outright" rule. That
// rule existed to stop a 700 ms guess contaminating a measurement; since v0.126.0
// the starting value IS a measurement -- 469 ms, timed off the engine -- so there
// is nothing to escape from, and taking a first sample outright would throw away
// the better estimate to chase a single reading.
static unsigned LadderLearnInterval(unsigned prevMs, unsigned gapMs)
{
    const unsigned folded = LadderFoldGap(gapMs, prevMs);
    if (folded == 0) return prevMs;
    return LadderClampInterval((prevMs * 2u + folded) / 3u);
}

// True while the engine is sounding this climb itself. The mod stays silent:
// its job is the ladders the game forgot, not a second layer on the ones it
// remembered.
//
// v0.127.0 widens the slack from two intervals to four. At two, the 1359 ms hole
// in the engine's own rhythm on crtower3 at 16:51:22 was long enough for the mod
// to cut in, and the log duly shows "2 mod steps, engine sounded it itself" on a
// ladder the mod was supposed to leave alone. Four intervals is ~1.9 s, past the
// longest hole the engine has ever been observed to leave (1359 ms), so a real
// dropout is still covered and its own rhythm is not interrupted.
static const unsigned LADDER_ENGINE_QUIET_FACTOR = 4;

static bool LadderEngineIsStepping(bool everEngine, unsigned nowMs,
                                   unsigned lastEngineMs, unsigned intervalMs)
{
    if (!everEngine) return false;
    return (unsigned)(nowMs - lastEngineMs) < intervalMs * LADDER_ENGINE_QUIET_FACTOR;
}

// ============================================================================
// v0.127.0 (#centra): WHAT THE DESCENT ACTUALLY DOES, IF IT IS NOT A CLIMB
// ============================================================================
// v0.126.0 answered "why is the way down silent" with mode 2 and fixed it, and
// the fix has never once been exercised: the 16:36-16:55 run went UP crroof1's
// roof ladder at 16:54:29, arrived at the Ladder Down line at 16:54:52, and the
// log ends there. So the next descent has to be conclusive whichever way it
// goes, and there are two ways it can fail that the log cannot currently tell
// apart -- the descent setting a movement mode nobody has catalogued, and the
// descent setting no new mode at all because it is a plain scripted move rather
// than a ladder opcode.
//
// A first-sighting bitmask cannot separate those: modes 0 and 1 are seen within
// seconds of any field loading, so a descent that walks the character down
// through 0 and 1 logs nothing and looks exactly like a descent that never
// happened. What distinguishes them is the TRANSITION. Walking is 0->1 and 1->0
// and nothing else; anything a ladder does leaves its own pair.
//
// **THE TRACE RAN, IT ANSWERED, AND v0.131.8 REMOVED IT.** The descent at
// 17:32:36-43 produced no transition line at all, which is how the next block
// down knows the way down runs no ladder opcode. A diagnostic that has given up
// its answer is not evidence any more, it is noise on a hot path -- so the
// finding is written here and the code that found it is gone.

// ============================================================================
// v0.128.0 (#centra): THE WAY DOWN IS NOT A LADDER MOVE AT ALL
// ============================================================================
// Aaron, three builds running: "the sounds work going up but not down on that
// top ladder." Every fix so far assumed the descent was a ladder move that the
// mod was mishandling. It is not a ladder move. crroof1's script says so:
//
//   lad0 (Ladder Up)   method 5:  BTNTEST 0xC0 -> PREQEW(party, method 4)
//   lad1 (Ladder Down) method 5:  BTNTEST 0xC0 -> PREQEW(party, method 5)
//
//   party method 4 (UP):    anim 3; run anim; anim 4; **LADDER MOVE (0x026)**
//                           to (661,-622,19296); SET3 (456,-418,19496)
//   party method 5 (DOWN):  anim 3; run anim; anim 7; WAIT 15; WAIT 32;
//                           **SET3 (923,-896,18403)** -- and that is all
//
// The ascent runs the ladder opcode, which sets the movement mode to 3, which
// is what this whole cue watches. **The descent runs no ladder opcode.** Square
// played the climbing animation in place, waited a fixed number of frames, and
// teleported the character to the bottom. The movement mode never leaves 0.
//
// So the engine plays no ladder step going down -- and neither did the mod, for
// exactly the same reason. v0.126.0's mode-2 presentation and v0.127.0's
// transition trace were both looking for a mode that is never set. The trace
// earned its keep by saying so: the descent at 17:32:36-43 produced no
// transition line at all (v0.131.8 has since removed that trace, its question
// answered), and the catalog either side of it shows the player
// moving from the 9-triangle top zone to the 65-triangle bottom one.
//
// WHAT THE MOD CAN SEE INSTEAD IS THE CONTROL LOCK. Opcodes 0x04E and 0x04D
// bracket both party methods, and they are the engine's player-control pair:
//
//     0x04E  handler 0x0051DE90   mov byte [0x01CE4903], 1   <- control off
//     0x04D  handler 0x0051DDA0   mov byte [0x01CE4903], 0   <- control on
//
// One global byte, no hook required, true for exactly as long as the scripted
// move runs and false the frame it ends. That is a better end condition than
// any duration this project could have guessed at -- and guessing at durations
// is what produced the 260 ms in v0.124.0.
//
// IT IS GATED ON THE LADDER LINE, because control is locked for every cutscene
// and every door in the game. The mod already knows where the trigger lines are
// and what they are called -- it says "Ladder Down. Press X to use it" out loud.
// So the cue arms only when control goes down WHILE the player is standing on a
// trigger line this field calls a ladder. Standing somewhere else when a scene
// starts arms nothing.
static bool LadderNameIsLadder(const char* name)
{
    if (name == 0) return false;
    for (int i = 0; name[i] != '\0' && i < 64; i++) {
        const char* p = name + i;
        int k = 0;
        const char* want = "ladder";
        while (want[k] != '\0') {
            char c = p[k];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != want[k]) break;
            k++;
        }
        if (want[k] == '\0') return true;
    }
    return false;
}

// ============================================================================
// v0.129.0 (#centra): THE GATE COULD NEVER HAVE PASSED
// ============================================================================
// v0.128.0 read the ladder line's name out of s_capturedLines[].name. That
// field is written exactly once, in the SETLINE hook, and what it is written is
// `'\0'` -- the comment there says "Name resolved later in RefreshCatalog", and
// RefreshCatalog resolves names into the CATALOG, never back into the captured
// line. So LadderNameIsLadder() was asked about an empty string every frame of
// every ladder in the game and the cue could not arm once. The 17:58 run says
// so without ambiguity: the descent happened -- NAV-OBSERVE at 17:58:33 records
// the player moving 654 units in 27 ticks, which is the teleport -- and not one
// [LADDER] line was written.
//
// The names live in s_catalog[], where a trigger-line entry carries
// entityIdx = -200 - lineSlot, which is how the catalog dump prints
// "cat1 TRIGGER line1 center=(474,-447) name='Ladder Down'". That is the same
// data the mod already speaks out loud, so it is the data the gate now reads.
static bool LadderCatalogIsTriggerLine(int entityIdx, int lineCount, int* lineSlotOut)
{
    if (entityIdx > -200 || entityIdx <= -300) return false;
    const int slot = -(entityIdx + 200);
    if (slot < 0 || slot >= lineCount) return false;
    if (lineSlotOut) *lineSlotOut = slot;
    return true;
}

// ============================================================================
// v0.130.0 (#centra): ASK THE SCRIPT, NOT THE SYMPTOMS
// ============================================================================
// v0.129.0 armed on the button the ladder line waits on. The 18:15-18:21 run
// says why that is not good enough: three arms, all on 'Ladder Up', all from
// the use button, and **not one of them was a climb**. `grep -c "engine
// movement mode"` over that whole log returns 0. The drive had parked the
// player at t=1.02 and t=1.03 -- fractionally past the end of the line, which
// v0.122.0's 15% margin allows but the engine's touch zone does not -- so the
// press did nothing, and the mod played sixteen ladder steps over eight seconds
// while NAV-OBSERVE recorded him walking around with the arrow keys. A cue that
// fires when nothing happened is worse than one that stays silent.
//
// THE CONTROL LOCK WAS ALSO WRONG, and the same log says so: every one of those
// events ended with "control free". Whatever 0x01CE4903 is, it is not set for
// the length of these moves.
//
// SO STOP INFERRING. The field script states the move exactly:
//
//     lad0 (Ladder Up)   method 5:  BTNTEST 0xC0 -> PREQEW(party, method 4)
//     lad1 (Ladder Down) method 5:  BTNTEST 0xC0 -> PREQEW(party, method 5)
//
// PREQEW is opcode 0x019, handler 0x0051D530, and it is a WAIT: it returns 3
// while the requested method is still running, which makes the interpreter
// re-execute the same instruction on the next frame, and returns 1 only once
// that method has finished (0x0051D6E2). Its first argument is the CALLING
// entity -- the line itself.
//
// That is a per-frame heartbeat for exactly as long as the move lasts, from the
// exact frame it starts to the exact frame it ends, emitted by the game's own
// interpreter. A press that does not register never reaches it. No button, no
// control byte, no teleport threshold, no backstop, and no duration guessed at:
// every one of those was this project trying to infer from outside something
// the script says out loud.
static const unsigned LADDER_PREQEW_HOLD_MS = 200;   // a few frames of slack
static const int      LADDER_PREQEW_SLOTS   = 4;     // concurrent waits, ring

// The heartbeat is per-frame; the hold only has to outlive a dropped frame or
// two, never a whole move.
static bool LadderPreqewIsRecent(unsigned nowMs, unsigned stampMs, bool everStamped)
{
    if (!everStamped) return false;
    return (unsigned)(nowMs - stampMs) <= LADDER_PREQEW_HOLD_MS;
}

// ============================================================================
// v0.131.0 (#centra): THE MOD MUST NOT WRITE ENGINE STATE FROM ITS OWN THREAD
// ============================================================================
// Aaron: "heard sound climbing down but once I climbed down I couldn't move."
// He is right that it is the mod's doing, and the log says exactly where he
// ended up: tri 1, "2/80 triangles reachable", at (637,-684). That is a
// two-triangle island at **z 19296** -- and crroof1's party method 4, the
// ASCENT, is `LADDER MOVE 0x026 -> (661,-622,19296)`. He finished a DESCENT
// standing on the ascent's mid-ladder landing, an island with no walkable
// connection to anything, which is why he could not move.
//
// THE MOD PUT HIM THERE. v0.126.0 made the cue write the player's movement mode
// byte at +0x23C to 3 for the length of the call, so sub_00520260 would take its
// ladder branch, and put it back afterwards. That was written as if the two
// happened between one engine frame and the next. They do not:
// FieldNavigation::Update() runs on AccessibilityThread, the mod's OWN thread
// (src/dinput8.cpp), while the game updates entities on its own. Sample that
// byte inside our window -- nine to nineteen times per climb, every 469 ms --
// and the engine sees a player on a ladder and moves him toward the ladder
// destination still sitting in +0x1B4/+0x1B8/+0x1BC from the last ladder move.
// On this field that is (661,-622,19296). It is not a coincidence that it is
// where he was standing.
//
// SO THE CUE STOPS TOUCHING THE ENTITY ENTIRELY. The engine's step routine is
// only a wrapper: it works out which sound a footfall should make and hands it
// to the sound player. The mod already knows the answer for a ladder, read out
// of the branch itself at 0x005203BB -- sound id 0x3F -- so it calls the player
// directly and skips the wrapper:
//
//     sub_0046B2A0(id, selector, pan, volume)     cdecl, four args
//     0x005203BB:  push vol; push pan; push 0x400000; push 0x3F; call 0x46B2A0
//
// That routine is pure audio: it maps the selector to a channel, stores the id,
// and calls the mixer. No entity pointer, no movement mode, no bone lookup, no
// projection -- and therefore nothing the game thread can catch half-written.
// The volume is 0x77, which is the LOUDEST the engine's own attenuation can ever
// produce (0x7F minus its minimum distance term of 8), so the cue can never be
// louder than a real footstep; the pan is the centre value all four engine call
// sites pass.
//
// The read-only hook on sub_00520260 stays: it runs on the game's thread, writes
// nothing, and is how the cadence gets measured.
static const unsigned LADDER_SOUND_PLAY_ADDR = 0x0046B2A0u;
static const int      LADDER_SOUND_ID        = 0x3F;       // from 0x005203BB
static const int      LADDER_SOUND_SELECTOR  = 0x400000;   // as the branch passes
static const int      LADDER_SOUND_PAN       = 0x80;       // centre
static const int      LADDER_SOUND_VOL       = 0x77;       // 0x7F - the minimum attenuation

// Twelve bytes of sub_0046B2A0's prologue, checked before the mod calls through
// a hardcoded address. A repacked exe fails and the cue stays silent, which is
// the only safe answer when the alternative is jumping into the middle of
// something else.
static const unsigned char LADDER_SOUND_SIG[] = {
    0x8B, 0x44, 0x24, 0x08,              // mov  eax, [esp+8]      <- the selector
    0x56,                                // push esi
    0x25, 0xFF, 0xFF, 0xFF, 0x00,        // and  eax, 0x00FFFFFF
    0x50,                                // push eax
    0xE8                                 // call ...
};
static const int LADDER_SOUND_SIG_LEN = (int)sizeof(LADDER_SOUND_SIG);

// The routine's first fifteen bytes, checked before MinHook is pointed at the
// address -- and it has to be checked FIRST, because installing the hook
// overwrites the very prologue the check reads. Since v0.131.0 the mod never
// calls this routine; it only listens to it, to measure the game's own cadence.
// A patched or repacked exe fails the check, the hook is not installed, and the
// cue falls back to the measured default rather than trampling something else.
static const unsigned char LADDER_STEP_SIG[] = {
    0x83, 0xEC, 0x10,                    // sub  esp, 0x10
    0x57,                                // push edi
    0x8B, 0x7C, 0x24, 0x18,              // mov  edi, [esp+0x18]   <- the entity
    0xF6, 0x87, 0x60, 0x01, 0x00, 0x00, 0x80   // test byte [edi+0x160], 0x80
};
static const int LADDER_STEP_SIG_LEN = (int)sizeof(LADDER_STEP_SIG);
static const unsigned LADDER_STEP_ADDR = 0x00520260u;

// The entity movement modes a ladder move sets. Anything else -- standing,
// walking, running -- is the engine's business and already makes its own noise.
static bool LadderModeIsClimb(int mode)
{
    return mode == 2 || mode == 3 || mode == 4;
}

// One step per learned interval while the climb lasts. `everStepped` is a flag
// rather than a zero timestamp for the reason the host probe found within a
// minute of existing: under a clock reading 0, a zero sentinel means the very
// first step stamps 0 and the spacing never engages. `climbStartMs` is stamped
// the frame the mode turns into a ladder mode, so the grace period is measured
// from the mount rather than from whenever the last climb happened to end.
static bool LadderStepDue(bool climbing, unsigned nowMs, unsigned climbStartMs,
                          unsigned lastMs, bool everStepped, unsigned intervalMs)
{
    if (!climbing) return false;
    if (!everStepped) return (unsigned)(nowMs - climbStartMs) >= LADDER_STEP_GRACE_MS;
    return (unsigned)(nowMs - lastMs) >= intervalMs;
}

// (The alternating-foot argument the engine's own callers pass went with the
// wrapper in v0.131.0. sub_0046B2A0 has no feet -- it is handed a sound id.)
