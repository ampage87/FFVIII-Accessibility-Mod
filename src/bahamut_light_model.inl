// bahamut_light_model.inl -- the PURE model of the Deep Sea Research Center's
// blue-light puzzle, the one that guards Bahamut.
//
// Included from bahamut_light_overlay.cpp, and compiled standalone by
// tests/bahamut_light_test.cpp. Nothing here touches game memory, Windows or
// the screen reader.
//
// ============================================================================
// AARON'S DESCRIPTION, AND WHAT THE SCRIPT SAYS
// ============================================================================
//
// Aaron: *"There is a mini-game / puzzle where you must walk toward the glowing
// blue light in order to trigger the dialogue with Bahamut... The trick is that
// the light pulsates, and you are supposed to walk only when the light has
// pulsated off. You have to do a stop-go thing where you go when the light is
// off and stop when it is on."*
//
// The field is **`sdcore1`, id 846** ("Deep Sea Research Center - Lobby 1"),
// and it holds Bahamut's whole scene: `Bahamu::bahamu0..bahamu4`, the
// `BossBattle` trigger line, and messages 17-27 -- "The blue light leads all to
// death.  Turn back...", "You have perceived the resonance...", "Damned
// imbeciles.  Why do you wish to fight?".
//
// Two other DSRC fields were checked and ruled out. `sdcore2` (847) has a
// `light` entity with `tukeru`/`kesu` (turn on / put out) methods, but its
// `Director` gates on `var[84] == 846` -- it is the room you arrive in AFTER
// Bahamut, and its light is scenery. None of `ddruins1-6`, `ddsteam1` or
// `ddtower1-6` names a light entity at all.
//
// ============================================================================
// THE PUZZLE IS TWO BYTES, AND THE SCRIPT WRITES BOTH
// ============================================================================
//
// `Director::default` in sdcore1 is a `while (true)` from dword 623 to 722.
// One iteration, transcribed (offline/jsmdis.py; opcode encoding per
// field_archive_jsm_decode.inl):
//
//     625  RANDOM -> local0                 ; three equally likely pulse tails
//     626  if local0 <= 86:                 ; (172 and else are the other two)
//     630     SOUND(40, 128, 64)            ; the wind-up, pitch 40/80/120
//     634     REQ core::speed               ; the light begins to brighten
//     637     WAIT 18
//     639     if var[1028] == 0:
//     643         var[1024] = 1             ; <-- ARMED
//     645     WAIT 110
//     647     REQ core::down                ; the light begins to fade
//     650     WAIT 48
//     652     var[1024] = 0                 ; <-- SAFE
//     654     WAIT 12
//     656     WAIT 30                       ; 30 / 20 / 5, by the random pick
//     658  goto 623
//
// **`var[1024]` (0x01CFEDB8) IS THE PUZZLE.** Two entities do nothing but watch
// it -- `battlekun1::default` and `battlekun4::default`, identical but for one
// mask:
//
//     while (true)
//         if var[1024] == 1 and BTNTEST(0x1000):       ; 0x2000 in battlekun4
//             r = RANDOM
//             BATTLE(318 | 319 | 320, 128)             ; by the same thirds
//             WAIT 45
//
// So the penalty is a random encounter, and it fires only while the byte is 1
// and a direction is held. Nothing else in the field reads it. That is why the
// mod does not need to see the screen, count frames or find the light's model:
// **the game keeps the answer in a byte, and the byte is exact.**
//
// `var[1028]` (0x01CFEDBC) is the game's OWN disarm, and it is not ours to
// invent. Three trigger lines -- `Hojo::touch`, `HojoHojo1::touch`,
// `HojoHojo2::touch` -- are seven dwords each and all three are the same:
//
//     var[1028] = 1 ; var[1024] = 0 ; op 0x3B
//
// They are the safe pockets along the corridor. While var[1028] is non-zero the
// Director's `if var[1028] == 0` fails and the light NEVER arms, so the walk is
// free. `Director::default` clears it on entry to the field, and `Koe::touch`
// -- the mid-corridor Bahamut voice line -- clears it again and re-arms
// var[1024] behind itself.
//
// THE SKIP IS THEREFORE THE GAME'S OWN SAFE POCKET, HELD OPEN. Nothing is
// forged: the mod writes exactly the pair of bytes the Hojo lines write, and
// holds them, because Koe::touch will otherwise take them back mid-corridor.
// Every other consequence of the scene -- Bahamut's dialogue, the AASK, the
// battle, the GF -- is downstream of `BossBattle::touch`, which does not read
// either byte and which opens with `var[1024] = 0` of its own accord.
//
// ============================================================================
// THE WINDOWS, AND WHY THE CUE IS AN EDGE
// ============================================================================
//
// From the transcript above, in script frames:
//
//     armed   110 + 48                        = 158   always
//     safe    18 (brightening) + 12 + tail     = 60 / 50 / 35
//
// The light is DANGEROUS for two and a half to three times as long as it is
// safe, and the safe window is the short one -- which is the whole difficulty
// of the puzzle and the reason a sighted player watches it so intently.
//
// The mod cannot lengthen that window, so it must not waste any of it. The cue
// fires on the EDGE of var[1024], not on a timer and not on a prediction: the
// instant the byte clears, the safe window has begun and the words are "Go".
// There is no lead time to give -- var[1024] goes to 1 eighteen frames after
// the wind-up sound the script plays, and the mod has no way to see that sound.
//
// What this module DOES do is measure both phases in milliseconds and log them,
// because a predictive cue (a "ready" a beat before the light clears) is worth
// building on measured numbers and worth nothing built on a guessed frame rate.
// One BAT hands those back.
// ============================================================================

namespace BahamutLightModel {

// ---- the field -------------------------------------------------------------
static const char* const BL_FIELD    = "sdcore1";
static const unsigned    BL_FIELD_ID = 846u;

// ---- the variables ---------------------------------------------------------
static const unsigned BL_VAR_BASE  = 0x01CFE9B8u;
static const int      BL_VAR_LIGHT = 1024;   // 1 = the light is lethal
static const int      BL_VAR_SAFE  = 1028;   // non-zero = the pulse is disarmed
// `Director::default` runs its arrival cutscene only while this is 1, and
// writes 2 at the end of it. So "not 1" means the scene is not playing.
static const int      BL_VAR_STORY = 614;

// ---- the windows, in script frames -----------------------------------------
static const int BL_F_BRIGHTEN = 18;    // light rising, byte still 0
static const int BL_F_HOLD     = 110;   // armed, light full
static const int BL_F_FADE     = 48;    // armed, light falling
static const int BL_F_DARK     = 12;    // safe, fixed part of the tail
static const int BL_F_TAIL_LO  = 5;     // the three random tails
static const int BL_F_TAIL_MID = 20;
static const int BL_F_TAIL_HI  = 30;

static int BlArmedFrames() { return BL_F_HOLD + BL_F_FADE; }
static int BlSafeFrames(int tail) { return BL_F_BRIGHTEN + BL_F_DARK + tail; }

// ---- the lead time on Stop -------------------------------------------------
//
// v0.101.0. Aaron: *"There isn't enough time from when Stop is announced to
// actually stop. It seems almost synchronous with my getting attacked."*
//
// It was EXACTLY synchronous, and by construction. v0.98.0 fired the cue on the
// var[1024] 0 -> 1 edge, and that edge IS the instant `battlekun1`/`battlekun4`
// go live: the same frame the mod says "Stop" is the first frame a held
// direction becomes an encounter. There was never any lead at all -- and speech
// costs a couple of hundred milliseconds before the first syllable lands, so the
// player heard it from inside the danger.
//
// THE ARM IS PREDICTABLE FROM THE GO EDGE, WITHIN THE RANDOM TAIL. From the Go
// edge (var[1024] 1 -> 0 at script step 652) the loop runs `WAIT 12`, then the
// random tail, then `WAIT 18` before it arms. So:
//
//     arm = Go + 30 + tail frames,  tail in {5, 20, 30}
//         = Go + 35 / 50 / 60 frames
//
// The tail is drawn fresh each cycle and nothing observable carries it, so the
// only safe deadline is the EARLIEST of the three. The cue therefore fires a
// fixed lead before Go + 35 frames, and on the longer cycles the player stops
// early. Stopping early costs walking distance; stopping late costs a battle.
//
// THE FRAME RATE IS MEASURED, NOT ASSUMED. The 2026-08-26 BAT logged the armed
// phase at 5,297 ms against 158 script frames -- 33.5 ms a frame -- and the safe
// windows at 1,203 / 1,703 / 1,734 / 1,859 / 1,875 / 2,063 ms against 35, 50 and
// 60. Three clusters, three tails, one frame rate: **30 Hz**.
//
// WHAT THIS BUYS, HONESTLY: the shortest safe window is 1,167 ms and the cue now
// lands about 800 ms into it, so Guide gives roughly half a second of walking a
// cycle. That is slow, and it is the ceiling: a spoken cue and a spoken reply
// cannot fit inside a window this short, which is why Skip exists and why the
// mod holds the light off while it is asking anything.
static const int      BL_SAFE_MIN_FRAMES = 35;      // 12 + tail(5) + 18
static const unsigned BL_FRAME_MS        = 33u;     // measured: 158 frames = 5,297 ms
// How far ahead of the arm the cue has to start so the word lands before it.
// Covers the screen reader's start-up latency plus a beat to react in.
static const unsigned BL_STOP_LEAD_MS    = 350u;

// How long after the Go edge to speak, given how long the safe window is and
// how much lead the speech needs. With the script retimed the window is a known
// constant and the lead can be generous; without it, the window is the earliest
// of three random tails and the lead has to be small enough to fit inside it.
static unsigned BlWarnDelayMs(int safeFrames, unsigned leadMs)
{
    const unsigned window = (unsigned)safeFrames * BL_FRAME_MS;
    return (window > leadMs) ? (window - leadMs) : 0u;
}

static unsigned BlStopWarnDelayMs()
{
    return BlWarnDelayMs(BL_SAFE_MIN_FRAMES, BL_STOP_LEAD_MS);
}

// Fires once per safe window, on a timer armed at the Go edge.
static bool BlStopWarnDue(bool inGo, bool alreadyWarned, unsigned sinceGoMs,
                          unsigned delayMs)
{
    return inGo && !alreadyWarned && sinceGoMs >= delayMs;
}

// The edge is now a BACKSTOP, not the cue. If the early warning fired, saying
// "Stop" again at the arm is noise the player has already acted on. If it did
// NOT fire -- a battle or a scene suspended the field script between the two --
// the edge is the only warning there is going to be, and late beats silent.
static bool BlSpeakOnArm(bool alreadyWarned) { return !alreadyWarned; }

// ---- what the player should be doing ---------------------------------------
enum BlCue {
    BL_CUE_NONE  = 0,   // say nothing (no reading yet)
    BL_CUE_GO    = 1,   // the light is off -- walk
    BL_CUE_STOP  = 2,   // the light is on -- stand still
    BL_CUE_CLEAR = 3    // a Hojo line disarmed the pulse -- walk freely
};

// The safe byte WINS over the light byte, and that is not a preference: while
// var[1028] is set the Director cannot write var[1024] at all, so a stale 1
// left there by Koe::touch would otherwise have the mod calling "Stop" down a
// corridor where nothing can hurt the player.
static BlCue BlCueFor(bool readOk, int lightVar, int safeVar)
{
    if (!readOk) return BL_CUE_NONE;
    if (safeVar != 0) return BL_CUE_CLEAR;
    return (lightVar == 1) ? BL_CUE_STOP : BL_CUE_GO;
}

static const char* BlCueText(BlCue c)
{
    switch (c) {
        case BL_CUE_GO:    return "Go";
        case BL_CUE_STOP:  return "Stop";
        case BL_CUE_CLEAR: return "Safe stretch. Keep walking to the light.";
        default:           return "";
    }
}

// Announce on the EDGE only. A cue that has not changed is a cue the player is
// already acting on, and repeating it into a 35-frame window is worse than
// silence.
static bool BlCueChanged(BlCue prev, BlCue now)
{
    return now != BL_CUE_NONE && now != prev;
}

// ---- getting the word out in time ------------------------------------------
//
// A queued "Go" is a lie: by the time it reaches the player the window it was
// describing has closed. So a cue that cannot be spoken PROMPTLY is dropped
// rather than queued, and the next edge will carry the truth.
//
// The one thing worth interrupting is our own previous cue, which is why the
// decision needs to know whether the last thing this module spoke is recent
// enough to still be what is on the wire. Anything else that is speaking is a
// story line -- Bahamut's own voice comes down this corridor via `Koe::touch`
// -- and cutting one of those off is not a trade worth making for a word the
// next edge will repeat.
enum BlSpeakAct {
    BL_SPEAK_DROP      = 0,
    BL_SPEAK_QUEUE     = 1,
    BL_SPEAK_INTERRUPT = 2
};

// Longer than either cue can take to say, and shorter than the armed window.
static const unsigned BL_OWN_MS = 1500u;

static BlSpeakAct BlSpeakDecision(bool busy, bool spokeLast, unsigned sinceMs,
                                  unsigned ownMs)
{
    if (!busy) return BL_SPEAK_QUEUE;
    if (spokeLast && sinceMs < ownMs) return BL_SPEAK_INTERRUPT;
    return BL_SPEAK_DROP;
}

// ---- retiming the script itself --------------------------------------------
//
// v0.103.0. Aaron: *"Can we make the light on-screen follow the same timing
// pattern? That way a low-vision player, who might be able to see a bit but is
// using the mod for ease of play, wouldn't get contradictory information."*
//
// The v0.102.0 metronome drives var[1024] but leaves the light animation on the
// script's own beat, because the animation is `REQ core::speed` / `REQ
// core::down` and has nothing to do with the byte. Two clocks, two stories. For
// a player who can see some of the light, that is worse than either alone.
//
// THE FIX IS TO RETIME THE SCRIPT, WHICH MEANS FINDING IT. The engine's opcode
// fetch is four instructions at 0x0052A621, disassembled from FF8_EN.exe:
//
//     0052A621  mov  cx, word [esi + 0x176]      ; the VM's instruction pointer
//     0052A629  mov  edx, dword [0x1d9cf50]      ; <-- THE LOADED CODE ARRAY
//     0052A634  lea  eax, [edx + ecx*4]          ; &code[ip]
//     0052A638  call 0x530760                    ; the decoder
//
// So `*(uint32_t**)0x01D9CF50` is the field's script, dword-indexed by exactly
// the indices offline/jsmdis.py prints. Every WAIT in `Director::default` is a
// `PSHN_L <frames>` immediately before a bare `WAIT` (0x3C), and a PSHN_L is
// `0x07000000 | frames`. Change the immediate and the loop keeps its shape and
// changes its tempo -- the light animation, the byte and the sound all move
// together because they are all the same loop.
//
// THE RETIMED CYCLE, AND WHY THESE NUMBERS. The point is that a low-vision
// player can trust what they see, so the byte has to agree with the picture:
// **bright means armed, dark means safe.** `core::speed` and `core::down` are
// both 60-frame fades, so the arm has to land where the fade-in finishes and
// the disarm where the fade-out does.
//
//     t=0    REQ core::speed      the light starts to brighten (60-frame fade)
//     t=33   the mod says "Stop"  (900 ms of lead, light visibly rising)
//     t=60   var[1024] = 1        ARMED -- and the light is now fully bright
//     t=90   REQ core::down       the light starts to dim (60-frame fade)
//     t=150  var[1024] = 0        SAFE -- and the light is now dark
//     t=195  next cycle
//
// safe  = 12 (dark hold) + 33 (tail) + 60 (fade-in) = 105 frames = 3,500 ms
// armed = 30 (bright hold) + 60 (fade-out)          =  90 frames = 3,000 ms
//
// The same numbers the metronome used, now produced by the game itself.
//
// AND THE THREE BUCKETS BECOME ONE. `Director::default` picks one of three tails
// at random (30 / 20 / 5 frames, with bucket C carrying an extra 19-frame
// pre-arm wait), which is what forced v0.101.0 to plan against the earliest of
// three. All three are set to the same tempo, so the safe window is now a known
// constant and the cue can be exact instead of conservative. What is NOT
// equalised is the wind-up sound's pitch -- 40 / 80 / 120 -- because that is
// flavour, not timing, and a corridor that sounds identical every cycle is worse
// than one that does not.
//
// NOTHING IS PATCHED WITHOUT A SIGNATURE. Thirty-five dwords are verified before
// twelve are written: the three `REQ core::speed` calls, every PSHN_L this
// touches, and every bare WAIT that follows one. If a single word is not exactly
// what sdcore1's own file says it should be -- a different release, a different
// field loaded into the same array, a pointer that went stale -- nothing is
// written and the module falls back to the metronome.
static const uint32_t BL_SCRIPT_CODE_PTR = 0x01D9CF50u;   // *(uint32_t**) -> code[]

// v0.105.0: A BATTLE UNDOES THE PATCH. The v0.104.0 BAT retimed the loop at
// 14:55:28 and the cycles came out at 3,562 / 3,032 ms -- the tempo asked for,
// with the screenshots to match: `bahamut_05_armed` is a blazing white light and
// `bahamut_06_safe`, three seconds later, is nearly dark. Then a random encounter
// at 14:56:10 tore the field down, `[fieldload] id=846 name='sdcore1'` rebuilt
// it, and from 14:57:46 the log reads `Stop held 5,297 ms` and `Go held 1,703
// ms`: the script's own numbers, back again. **The field script is reloaded from
// the archive on the way back from a battle**, so a patch applied once per visit
// lasts exactly until the first encounter -- and an encounter is the thing this
// puzzle is made of.
//
// So the retiming is re-checked rather than done once. The steady-state cost is
// twelve reads: the loop's address is remembered, and only when the words there
// stop reading as ours does the full window search run again.
static const unsigned BL_SCRIPT_RECHECK_MS = 1000u;

// What a re-check found.
enum BlPatchResult {
    BLP_FAIL    = 0,   // not found, or a write was refused
    BLP_ALREADY = 1,   // the loop is already running at the mod's tempo
    BLP_WROTE   = 2    // written just now
};

// Worth a log line only when the answer changes. A retiming re-applied after a
// battle IS worth one every time, because how often that happens is the whole
// reason this re-check exists.
static bool BlPatchWorthLogging(int result, bool wasPatched)
{
    if (result == (int)BLP_WROTE) return true;             // first time, or again
    if (result == (int)BLP_FAIL && wasPatched) return true; // lost it
    return false;
}

static uint32_t BlPshn(int frames) { return 0x07000000u | (uint32_t)frames; }
static const uint32_t BL_WAIT_WORD = 0x0000003Cu;   // bare opcode 0x3C
static const uint32_t BL_REQ_CORE  = 0x14000011u;   // REQ entity 17 (core)

// The retimed cycle, in script frames.
static const int BL_RT_FADE      = 60;   // both fades are 60 frames
static const int BL_RT_BRIGHT    = 30;   // held at full brightness
static const int BL_RT_DARKHOLD  = 12;   // the script's own, unchanged
static const int BL_RT_TAIL      = 33;
static const int BL_RT_C_SPLIT   = 19;   // bucket C's second pre-arm wait, kept
static int BlRtSafeFrames()  { return BL_RT_DARKHOLD + BL_RT_TAIL + BL_RT_FADE; }
static int BlRtArmedFrames() { return BL_RT_BRIGHT + BL_RT_FADE; }

// A word that must read exactly this before anything is written.
struct BlSigWord { int idx; uint32_t word; };

// A word that is verified and then replaced.
struct BlPatchWord { int idx; uint32_t from; uint32_t to; };

// Verified only: the anchors that prove this is sdcore1's Director loop.
static const BlSigWord BL_SIG[] = {
    { 636, BL_REQ_CORE }, { 638, BL_WAIT_WORD }, { 646, BL_WAIT_WORD },
    { 651, BL_WAIT_WORD }, { 654, 0x0700000Cu }, { 655, BL_WAIT_WORD },
    { 657, BL_WAIT_WORD },
    { 669, BL_REQ_CORE }, { 671, BL_WAIT_WORD }, { 679, BL_WAIT_WORD },
    { 684, BL_WAIT_WORD }, { 687, 0x0700000Cu }, { 688, BL_WAIT_WORD },
    { 690, BL_WAIT_WORD },
    { 698, BL_REQ_CORE }, { 700, BL_WAIT_WORD }, { 701, 0x07000013u },
    { 702, BL_WAIT_WORD }, { 710, BL_WAIT_WORD }, { 715, BL_WAIT_WORD },
    { 718, 0x0700000Cu }, { 719, BL_WAIT_WORD }, { 721, BL_WAIT_WORD },
};
static const int BL_SIG_COUNT = (int)(sizeof(BL_SIG) / sizeof(BL_SIG[0]));

// Verified and rewritten. Bucket C's pre-arm is split across two waits (18 + 19)
// and only the first is moved, so no WAIT is ever set to zero.
static const BlPatchWord BL_PATCH[] = {
    // pre-arm: the fade-in has to finish exactly when the byte arms.
    { 637, 0x07000012u, 0x0700003Cu },                       // A: 18 -> 60
    { 670, 0x07000012u, 0x0700003Cu },                       // B: 18 -> 60
    { 699, 0x07000012u, 0x07000029u },                       // C: 18 -> 41 (+19 = 60)
    // armed, held bright.
    { 645, 0x0700006Eu, 0x0700001Eu },                       // A: 110 -> 30
    { 678, 0x0700006Eu, 0x0700001Eu },                       // B
    { 709, 0x0700006Eu, 0x0700001Eu },                       // C
    // armed, fading out -- the byte clears as the light reaches dark.
    { 650, 0x07000030u, 0x0700003Cu },                       // A: 48 -> 60
    { 683, 0x07000030u, 0x0700003Cu },                       // B
    { 714, 0x07000030u, 0x0700003Cu },                       // C
    // the random tail, made constant.
    { 656, 0x0700001Eu, 0x07000021u },                       // A: 30 -> 33
    { 689, 0x07000014u, 0x07000021u },                       // B: 20 -> 33
    { 720, 0x07000005u, 0x07000021u },                       // C:  5 -> 33
};
static const int BL_PATCH_COUNT = (int)(sizeof(BL_PATCH) / sizeof(BL_PATCH[0]));

// The highest dword index either table touches -- the caller must not read past
// the end of the code array.
static int BlScriptMaxIndex()
{
    int m = 0;
    for (int i = 0; i < BL_SIG_COUNT; i++)   if (BL_SIG[i].idx   > m) m = BL_SIG[i].idx;
    for (int i = 0; i < BL_PATCH_COUNT; i++) if (BL_PATCH[i].idx > m) m = BL_PATCH[i].idx;
    return m;
}

// Every verified word, and every word about to be written, must read exactly
// what sdcore1's own file says. `alreadyPatched` reports the case where the
// signature words all match but the patch words already hold their new values --
// a re-entry to the field without a reload -- so the caller can skip the write
// instead of treating it as a mismatch.
static bool BlScriptMatchesAt(const uint32_t* buf, int count, int base,
                              bool* alreadyPatched)
{
    if (alreadyPatched) *alreadyPatched = false;
    if (buf == nullptr) return false;
    if (base < 0 || base + BlScriptMaxIndex() >= count) return false;
    for (int i = 0; i < BL_SIG_COUNT; i++)
        if (buf[base + BL_SIG[i].idx] != BL_SIG[i].word) return false;
    int asFound = 0, asPatched = 0;
    for (int i = 0; i < BL_PATCH_COUNT; i++) {
        const uint32_t w = buf[base + BL_PATCH[i].idx];
        if      (w == BL_PATCH[i].from) asFound++;
        else if (w == BL_PATCH[i].to)   asPatched++;
        else return false;
    }
    if (asPatched == BL_PATCH_COUNT) { if (alreadyPatched) *alreadyPatched = true; return true; }
    return asFound == BL_PATCH_COUNT;
}

static bool BlScriptMatches(const uint32_t* code, int count, bool* alreadyPatched)
{
    return BlScriptMatchesAt(code, count, 0, alreadyPatched);
}

// ---- and finding it, because the index base is NOT ours ---------------------
//
// v0.104.0. The v0.103.0 BAT refused to patch, and it was right to: the words it
// read at 637 / 645 / 656 were `RET 8`, `EXT_DISPATCH` and `PSHN_L 30`. Lining
// those three up against sdcore1's own file puts them at code[854], code[862]
// and code[873] -- **the array behind `*(uint32_t**)0x01D9CF50` is offset from
// this file's dword indices by 217**, and there is no reason to believe 217 is a
// constant. It is not a script-structural boundary (code[217] is in the middle
// of Selphie::move3), so it is whatever that pointer happened to be at the
// moment it was read, not a base the entry table shares.
//
// So the loop is FOUND rather than assumed. The signature is thirty-five exact
// dwords spanning eighty-six positions, three of them `REQ core` and the rest
// specific PSHN_L values and bare WAITs; sliding that over a window either lands
// on the Director's loop or lands nowhere. **Exactly one match is required** --
// two would mean the pattern is less specific than it looks, and the right
// answer then is to write nothing.
static int BlScriptScan(const uint32_t* buf, int count, int* outBase,
                        bool* alreadyPatched)
{
    if (outBase) *outBase = -1;
    if (alreadyPatched) *alreadyPatched = false;
    if (buf == nullptr) return 0;
    int hits = 0;
    // An optimisation, not a guard: BlScriptMatchesAt bounds-checks every base it
    // is handed, so a wrong value here costs iterations and nothing else.
    const int last = count - BlScriptMaxIndex() - 1;
    for (int b = 0; b <= last; b++) {
        bool ap = false;
        if (!BlScriptMatchesAt(buf, count, b, &ap)) continue;
        hits++;
        // Recorded unconditionally. A first-match-only guard here was dead: the
        // refusal below clears the base on anything other than a single hit, so
        // "which match won" cannot be observed. Removed rather than left standing.
        if (outBase) *outBase = b;
        if (alreadyPatched) *alreadyPatched = ap;
    }
    if (hits != 1) {
        if (outBase) *outBase = -1;
        if (alreadyPatched) *alreadyPatched = false;
    }
    return hits;
}

// ---- the metronome ---------------------------------------------------------
//
// v0.102.0. Aaron: *"Is it possible to add something observable so we can more
// accurately predict when the player needs to stop? Alternatively, could we
// overwrite the light's timer to make it a little bit longer so it is playable
// by a blind player?"*
//
// THERE IS AN OBSERVABLE AND IT IS NOT ENOUGH. The wind-up is real: `REQ
// core::speed` fires eighteen frames -- 600 ms -- before the byte arms, and the
// engine's opcode dispatch table at 0x00B8DE94 would let the mod hook it and
// know the arm instant exactly instead of planning against the earliest of three
// random tails. But the shortest safe window is 1,167 ms, and a spoken "Go", a
// human reaction, a spoken "Stop" and a second reaction do not fit inside it
// even with a perfect prediction. Better information does not make a window that
// short playable; it only makes the mod later by less.
//
// SO THE MOD TAKES THE CLOCK. `Director::default` only arms the light through
// one line -- `if var[1028] == 0 then var[1024] = 1` -- and holding var[1028] at
// 1 is the game's own way of saying "not here": it is exactly what the
// corridor's three `Hojo` safe-pocket lines write. With the Director shut out of
// the byte, the mod drives var[1024] itself on a tempo a person can actually
// play, and `battlekun1`/`battlekun4` go on punishing a held direction exactly
// as they always did. Same rule, slower clock.
//
// AND IT REMOVES THE RACE, which is the part that matters most. Extending the
// game's own cycle -- letting the Director set the byte and writing 0 back over
// it -- leaves one script frame per cycle where the byte is 1 and battlekun can
// read it before the mod's next poll. Over a twenty-cycle corridor that is a
// battle you did nothing to earn. Holding var[1028] means the Director never
// writes the byte at all and there is no frame to lose.
//
// THE NUMBERS, AND WHY THESE ONES. The safe phase has to carry a spoken "Go"
// (~250 ms before the first syllable lands), a reaction, some actual walking,
// and then the "Stop" lead. At 3,500 ms that is about 2.4 seconds of walking --
// roughly double the 1.0-1.7 s the game's own window leaves after speech, which
// is "a little bit longer" rather than a different game. The armed phase is
// 3,000 ms against the script's 5,267, because waiting is the part of this
// puzzle that was never the challenge.
//
// WHAT THIS DOES NOT DO: the light on screen still pulses to the script's own
// beat, because that animation is `REQ core::speed` / `core::down` and has
// nothing to do with the byte. A sighted onlooker would see the light and the
// mod disagree. For the player this module exists for, the byte is the truth.
// Frame-exact, so the metronome and the retimed script produce the SAME tempo:
// 105 and 90 script frames at BL_FRAME_MS. A test asserts the two agree, because
// two paths that tell the player different things is the bug this whole module
// keeps rediscovering.
static const unsigned BL_TEMPO_SAFE_MS  = 3465u;   // 105 frames
static const unsigned BL_TEMPO_ARMED_MS = 2970u;   //  90 frames
static const unsigned BL_TEMPO_LEAD_MS  = 900u;   // "Stop" this far before the arm

enum BlTempo {
    BL_TEMPO_OFF   = 0,   // not running (Skip, or the write failed)
    BL_TEMPO_SAFE  = 1,
    BL_TEMPO_ARMED = 2
};

// What var[1024] must read for a phase. This IS the phase, as far as the two
// watcher entities are concerned.
static int BlTempoLightVar(BlTempo phase)
{
    return (phase == BL_TEMPO_ARMED) ? 1 : 0;
}

// While the mod owns the byte the Director has to be kept out of it, and the way
// to do that is the pair the Hojo lines write.
static bool BlTempoHoldsDirectorOut(BlTempo phase)
{
    return phase != BL_TEMPO_OFF;
}

// The warning goes out one lead before the safe phase ends -- a real deadline
// the mod set itself, not a guess at the earliest of three random tails.
static bool BlTempoWarnDue(BlTempo phase, bool alreadyWarned, unsigned inPhaseMs,
                           unsigned safeMs, unsigned leadMs)
{
    if (phase != BL_TEMPO_SAFE || alreadyWarned) return false;
    const unsigned at = (safeMs > leadMs) ? (safeMs - leadMs) : 0u;
    return inPhaseMs >= at;
}

static bool BlTempoPhaseOver(BlTempo phase, unsigned inPhaseMs,
                             unsigned safeMs, unsigned armedMs)
{
    if (phase == BL_TEMPO_SAFE)  return inPhaseMs >= safeMs;
    if (phase == BL_TEMPO_ARMED) return inPhaseMs >= armedMs;
    return false;
}

// NEVER ARM OVER A SCENE. `Koe::touch` stops the party mid-corridor to let
// Bahamut speak, and `BossBattle::touch` is the whole ending; both take player
// control and put a window up. Arming into one of those would mean a held key
// from before the scene became an encounter the player could not have avoided.
static bool BlTempoMayArm(bool dialogOpen)
{
    return !dialogOpen;
}

// ---- the skip --------------------------------------------------------------
//
// The pair the Hojo lines write. Held every tick, because Koe::touch takes both
// back partway down the corridor and a skip that stops working halfway is worse
// than one that was never offered.
static bool BlSkipWriteNeeded(int lightVar, int safeVar)
{
    return lightVar != 0 || safeVar != 1;
}

// ---- when to ask, and how -------------------------------------------------
//
// v0.100.0: THE PROMPT IS NOT AN ENGINE DIALOG ANY MORE, AND THE 2026-08-26 BAT
// IS WHY. v0.98.0 opened a DialogInject ASK on window slot 2. Every message in
// this field and ALL THREE of Bahamut's AASKs are on window slot 2 -- there is
// no message opcode in sdcore1 that uses any other slot for him -- so the mod
// was borrowing the one window the scene cannot do without. The log shows what
// it cost: the ASK ran on slot 2 from 00:19:16 to 00:19:32, a random encounter
// tore the field down and rebuilt it, and from 00:19:50 window 2 came back
// `st=7 tr=4096` -- actively displayed -- holding stale text, and STAYED THERE
// for ninety-eight seconds until Aaron quit. Nothing owned it, so nothing ever
// closed it, and the Bahamut scene had a window it could not use.
//
// AND THE ASK GOT HIM INTO A BATTLE. Its cursor is driven by the arrow keys,
// and `battlekun1`/`battlekun4` throw a random encounter for a held direction
// while var[1024] is 1. Mode 1 -> 3 at 00:19:29, three seconds BEFORE the answer
// committed. The prompt the mod put up to help him cross the room is what
// dragged him into the fight.
//
// So the prompt is now spoken, answered with two keys that are nothing to do
// with movement, and repeated -- a missed prompt is not a lost prompt. It
// touches no engine window at all. And while it is pending the mod holds the
// light disarmed, writing the same pair the corridor's own Hojo lines write,
// so a prompt can never again be the reason the player is standing still in a
// lit corridor.

// The prompt repeats rather than being one-shot, because a single spoken line
// can be missed and there is no box left on screen to come back to.
static const unsigned BL_PROMPT_REPEAT_MS = 9000u;
static const int      BL_PROMPT_MAX       = 3;

// Ready in the same two ways v0.98.0 was -- the pulse proving itself, or the
// cutscene demonstrably idle and the party settled -- and then due again every
// BL_PROMPT_REPEAT_MS until it is answered or BL_PROMPT_MAX is reached.
static bool BlPromptArmed(bool inField, bool answered, bool pulseSeen,
                          bool cutsceneIdle, unsigned inFieldMs, unsigned settleMs)
{
    if (!inField || answered) return false;
    if (pulseSeen) return true;
    return cutsceneIdle && inFieldMs >= settleMs;
}

static bool BlPromptDue(bool armed, int spokenCount, int maxCount,
                        bool everSpoken, unsigned sinceLastMs, unsigned repeatMs)
{
    if (!armed) return false;
    if (spokenCount >= maxCount) return false;
    if (!everSpoken) return true;
    return sinceLastMs >= repeatMs;
}

// ---- the modes -------------------------------------------------------------
enum BlMode {
    BL_MODE_NONE  = 0,   // not answered yet / left the field
    BL_MODE_GUIDE = 1,   // the mod calls Go and Stop
    BL_MODE_SKIP  = 2    // the pulse is held disarmed
};

// The two keys. NOT the arrow keys, and not a key the field already means
// something by: `-`, `+`, `\\`, Backspace, Space, F9 and the arrows are all
// spoken for in field mode (field_nav_handlekeys.inl).
static BlMode BlKeyChoice(bool guideKey, bool skipKey)
{
    if (guideKey && skipKey) return BL_MODE_NONE;   // both down says nothing
    if (guideKey) return BL_MODE_GUIDE;
    if (skipKey)  return BL_MODE_SKIP;
    return BL_MODE_NONE;
}

// A prompt that has run out of repeats without an answer leaves the mod
// guiding, which is the mode that changes nothing about the game.
static BlMode BlModeOnTimeout() { return BL_MODE_GUIDE; }

// THE PROMPT MUST NOT BE WHAT GETS THE PLAYER KILLED. While it is pending the
// mod holds the same pair the Hojo safe pockets write, so a player standing
// still and listening cannot be caught by a pulse they were never told about.
static bool BlHoldSafeWhilePrompting(bool inField, bool answered)
{
    return inField && !answered;
}

}  // namespace BahamutLightModel
