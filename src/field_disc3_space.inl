// field_disc3_space.inl -- #111, the space rescue: closing on Rinoa.
// PART OF field_disc3.inl. Do NOT compile standalone.
//
// The whole game is "keep her centred", the verdict is taken once at the end of
// a 90-second approach, and the player cannot see any of it.
//
// v0.63.0 made it flyable and Aaron flew it: "Successfully completed the
// mini-game using manual navigation." That settles the steering, and everything
// below is the two things he asked for on the way out.
//
// 1. THE BEEP IS GONE.
//
//      "Let's get rid of the beep sound effect. It is extremely distracting and
//       the TTS announcement I think is sufficient."
//
//    Not just the radar pulse -- the two-note cue on entering the aim box goes
//    with it, because the module says "centred" on exactly that transition and
//    a cue that duplicates a sentence while covering the next one is worse than
//    nothing. What is left is words, at 3.5 seconds and 1.2 when she is close.
//
// 2. THE GAME CONTROLS SCREEN IS A REAL PAUSE.
//
//      "The Game Controls screen never had time to appear and announce. It was
//       interrupted by Squall's speech at the start, and it didn't freeze the
//       time for the player to read the controls either. The controls should
//       essentially pause everything until the player hits Enter to proceed."
//
//    Both halves failed for the same reason: v0.63.0 treated a 90-second clock
//    as the thing it had to fit inside. It waited for the scene's dialogue --
//    but tested "is a dialogue open" on the frame the field loaded, when Squall
//    had not started talking yet, so it briefed at once and his first line
//    landed on top of it three seconds later. And it took the screen away after
//    fifteen seconds whether or not anyone had read it, because the clock was
//    running underneath.
//
//    So stop the clock. CountdownTimer::SetHold pins the engine's own countdown
//    global from the moment the field loads, with the same rewrite-every-frame
//    machinery Shift+T has used since v0.15.13. Nothing then costs anything:
//    the brief can wait out all three of Squall's lines, the screen has no
//    timeout, and the player's own position is pinned too, so an arrow pressed
//    while reading does not fly the ship. Enter gives the clock back.
//
// tests/space_sim_test.cpp flies the whole approach on nothing but these words,
// on the measured 48-units-per-second step rather than the 120 v0.63.0 assumed.
//
// v0.63.2 -- three corrections, all Aaron's, after the flight that worked:
//
//   1. THIRD PERSON. "when I call" became "when the mod calls", to match the
//      Garden battle and the dragon fight, which have always said "Hold %s to
//      block, tap %s to attack" and never once said "I".
//   2. THE BOOST IS ON THE SCREEN, and the arrows are described as HELD. Both
//      were missing, and the second is why a new player mashes a direction and
//      moves four units per tap. The boost key is named from the same learner
//      the Garden battle uses -- mask 0x0010, which measured as W on Aaron's
//      keyboard, not the X he expected (that is 0x0040).
//   3. THE FUEL IS SPOKEN. var[1052] starts at 8000 and drains 4 a frame while
//      a direction is held, 8 while boosting; the script writes it to gauge 0,
//      which is the bar a sighted player watches. Nothing reads it back, so
//      the mod reports the GAUGE and does not claim the boost has stopped.
//
// v0.64.0 -- THE SCREEN IS A REAL PAUSE NOW.
//
//   "Is there no way we can suppress the actual scene behind the Game Controls
//    screen, that way when the player presses enter that is when Rinoa begins
//    to move?"
//
// There is, and v0.63.x's answer -- stop the mission clock and warn that the
// scene runs on regardless -- was the honest version of the wrong thing. The
// engine has one pause point and it is a real one: field_main_loop calls
// field_main only when two globals are zero, everything after that call still
// runs, and the call passes no arguments, so a one-byte RET at its entry stops
// the field outright. src/field_pause.inl holds the mechanism and the rules
// that keep it safe; this file holds the sequence:
//
//     arrive -> open the box and start speaking -> let a few field frames run
//     so the box is DRAWN -> freeze the field -> wait for Enter, forever if he
//     likes -> release, and the scene starts with him.
//
// That costs SP_DRAW_MS of scene instead of the forty-six seconds his last run
// spent. The dialogue wait is gone with it: Squall cannot interrupt a brief
// during a frame that never happens, so there is nothing left to wait for.
// Both are kept as the FALLBACK, chosen by FieldPause::Available() before the
// screen opens, because a build whose field_main does not read the way it
// should must still get the v0.63.3 behaviour rather than none.

namespace Space {

static const uint16_t SS_ID = SR_FIELD_ID;      // 878, derived; name is the backstop

static bool   s_on        = false;
static bool   s_briefed   = false;
static bool   s_skip      = false;
static bool   s_wasIn     = false;      // inside the aim box last tick
static DWORD  s_lastCall  = 0;
static char   s_lastWord[64] = "";
static int    s_lastWorst = -1;         // worse-axis error at the previous call

// The Game Controls screen.
static bool   s_screen    = false;      // the box is up
static DWORD  s_screenAt  = 0;
static DWORD  s_nudgeAt   = 0;
static bool   s_enterWas  = false;
static DWORD  s_arrivedAt = 0;
static DWORD  s_dlgLast   = 0;          // last tick a field dialogue was open
static bool   s_sawDlg    = false;      // ...and whether we ever saw one at all
static bool   s_held      = false;      // we have asked for the clock to stop
static bool   s_realPause = false;      // ...or we stopped the whole field instead
// v0.66.1 (#111): the presented-frame count when the freeze engaged, and a
// one-shot self-capture taken while the engine is still presenting.
static unsigned s_swapsAtFreeze = 0;
static bool     s_shotAsked     = false;
static DWORD  s_drawnAt   = 0;          // when the box went up, for the draw delay
static int32_t s_pinX = 0, s_pinY = 0;  // her position, held while the box is up
static int    s_fuelBand  = 3;          // last announced fuel band (3 = full)
static bool   s_hintedBoost = false;    // the "you could boost that" line, once
static int32_t s_lastX = 0, s_lastY = 0;   // for "nothing has moved since
static DWORD  s_holdStart = 0;          // when the clock actually stopped
static char   s_boostKey[24] = "";      // whatever key mask 0x0010 turned out to be
static char   s_screenText[512] = "";   // built at OpenScreen: it names that key

// WAITING OUT SQUALL. The scene opens with three lines of his own thoughts --
// "(Rinoa...... Where are you?)" at +2 s, "(I'm gonna find you...)" at +8,
// "(I have to get in front of her...)" at +15 in Aaron's 2026-08-22 log.
// Briefing over them means the mod interrupts the game and the game then
// interrupts the mod, and he hears two halves of two things. That is exactly
// what happened: "[SPACE] game controls open" at 16:10:54, Squall at 16:10:57.
//
// v0.63.0's wait was "no dialogue is open and it has been quiet for 1200 ms",
// which is TRUE ON THE FIRST FRAME OF A SCENE THAT HAS NOT STARTED TALKING.
// The fix is to distinguish the two silences: quiet counts only once we have
// SEEN a dialogue (so we know we are hearing the end of one) or once the scene
// has had SP_SETTLE_MS to open its first (so a scene with no dialogue at all is
// not waited on forever).
static const DWORD SP_DLG_QUIET_MS = 1200;
static const DWORD SP_SETTLE_MS    = 6000;
// And a cap, because a scene that never stops talking must not swallow the
// brief entirely. Which cap depends on whether the clock is actually stopped:
// if the hold took, waiting is free and we can sit through the whole opening;
// if it did not, we are spending a ninety-second budget and must not.
static const DWORD SP_BRIEF_CAP_MS      = 22000;
static const DWORD SP_BRIEF_CAP_HELD_MS = 45000;

// THE SCREEN DOES NOT TIME OUT. v0.63.0's fifteen seconds expired unread at
// 16:11:09. Aaron: "The controls should essentially pause everything until the
// player hits Enter to proceed." So: Enter, or nothing. The backstop below is
// not a timeout in any sense the player will meet -- it is there so a mod bug
// can never leave a box on screen forever, and F9 is read before the screen
// gate anyway, so the escape hatch is open the entire time.
// HOW LONG THE BOX GETS TO BE DRAWN BEFORE THE FIELD STOPS. Window rendering
// hangs off field_main, so a box opened and frozen in the same tick is a box
// that was never drawn. A fifth of a second is six frames at 30fps -- enough
// for the window to appear, and short enough that Squall (whose first line is
// two seconds in) cannot get a word out before the field stops.
static const DWORD SP_DRAW_MS = 200;
// The self-capture goes in ahead of the freeze, with room for the request to be
// serviced by a swap before the field stops presenting.
static const DWORD SP_SHOT_MS = 90;
// The ceiling on waiting for the game's window to finish typing. With the text
// speed at zero it finishes on the frame it opens, so this should never be
// reached -- and if it ever is, the log says so rather than the scene quietly
// running on.
static const DWORD SP_BOX_CAP_MS = 2000;

static const DWORD SP_SCREEN_CAP_MS = 600000;   // ten minutes
static const DWORD SP_NUDGE_MS      = 25000;    // "press Enter", periodically

// Cadence. Both are inside Aaron's "maybe a 3-5 second pause", and the near
// band tightens it where a call is worth more.
static const DWORD SP_CALL_MS  = 3500;
static const DWORD SP_NEAR_MS  = 1200;
static const int   SP_NEAR_BAND = 900;
// ...but not while he is parked ON the target. Aaron's 2026-08-23 attempt 2 was
// centred at 18:35:14 and stayed at x=16 y=-31 until the verdict at 18:36:17:
// sixty-three seconds, and the near band said "centred" about fifty times into
// it. The fast cadence exists to give resolution while the gap is CLOSING; once
// it is closed and nothing is moving there is nothing to resolve. Still spoken,
// because silence in a scene with no other feedback reads as a mod that died --
// just at a rhythm a person can sit inside, and as "still centred" after the
// first, which is the idiom the steering calls already use.
static const DWORD SP_HELD_MS  = 3000;

// ---------------------------------------------------------------------------
// THE CLOCK
// ---------------------------------------------------------------------------
// One owner, one release path. Every exit from the pre-flight -- Enter, F9, the
// backstop, walking out of the field -- goes through Hold(false), so there is
// no arrangement of them that leaves the engine's countdown pinned.
static void Hold(bool on, const char* why)
{
    if (on == s_held) return;
    s_held = on;
    const DWORD now = GetTickCount();
    if (on) s_holdStart = now ? now : 1;
    CountdownTimer::SetHold(on, why);
    Log::Field("FieldNavigation: [SPACE] clock %s (%s)%s", on ? "held" : "released", why,
               on ? "" : " -- see HeldSeconds()");
}

// How long the clock stood still, in seconds. Zero before it ever did.
static int HeldSeconds(DWORD now)
{
    if (!s_holdStart) return 0;
    return (int)((now - s_holdStart) / 1000);
}

// THE PAUSE, WHICHEVER ONE WE GOT. One entry point, one exit point, so no
// arrangement of Enter, F9, the backstop and walking out can leave either the
// field frozen or the clock pinned.
static void Unpause(const char* why)
{
    if (s_realPause) {
        // MEASURE THE CLAIM RATHER THAN REPEATING IT. A pause across which the
        // engine presented nothing is a pause during which nothing the mod draws
        // could reach the screen and nothing could be captured -- and that is
        // the finding the whole overlay saga turned on. Zero here proves it;
        // any other number disproves it, and either way it is one line.
        const unsigned after  = FieldOverlay::SwapCount();
        const unsigned frames = after - s_swapsAtFreeze;
        const int      secs   = (int)(HeldSeconds(GetTickCount()));
        // MEASURED, 13:12: ONE frame across a twenty-five second pause. Not the
        // literal zero the first draft asserted, and the difference does not
        // matter -- a handful of frames in half a minute is a stopped engine,
        // and it is why an F11 pressed during the pause is not serviced until
        // the swap after Enter. The line reports the number and the duration
        // and lets the reader draw the line, rather than declaring a threshold
        // I would only have to move.
        Log::Field("FieldNavigation: [SPACE] the engine presented %u frames in the %d s "
                   "the field was frozen%s", frames, secs,
                   (frames <= 2)
                       ? " -- effectively stopped, which is why the last frame drawn "
                         "before the freeze is the one that stays on screen and why a "
                         "capture asked for during the pause waits for Enter"
                       : " -- which is a running engine, so the freeze is not stopping "
                         "presentation and something about this scene has changed");
        FieldPause::Release(why);
        s_realPause = false;
    }
    Hold(false, why);
}

static void Reset()
{
    Unpause("reset");
    s_on = false; s_briefed = false; s_skip = false; s_wasIn = false;
    s_lastCall = 0; s_lastWord[0] = '\0'; s_lastWorst = -1;
    s_screen = false; s_screenAt = 0; s_nudgeAt = 0; s_enterWas = false;
    s_arrivedAt = 0; s_dlgLast = 0; s_sawDlg = false;
    s_pinX = 0; s_pinY = 0;
    s_fuelBand = 3; s_boostKey[0] = '\0'; s_screenText[0] = '\0';
    s_hintedBoost = false; s_holdStart = 0;
    s_lastX = 0; s_lastY = 0;
    s_realPause = false; s_drawnAt = 0;
    s_swapsAtFreeze = 0; s_shotAsked = false;
}

// A RETRY IS NOT A NEW FIELD.
//
// "Rinoa was lost in space...forever" -> Try again -> MAPJUMPO 878, which is
// ssspace3 again: `Here()` never goes false, so nothing above resets. Aaron's
// 2026-08-23 log caught two consequences at 18:34:47, both from the same cause.
// The scene tears down with var[1052] briefly reading 0, which the fuel tracker
// announced as "Boost fuel gauge empty" in the middle of a "Try again" prompt;
// and it then latched at empty, so the refilled 8000 of the second attempt
// could never announce anything again.
//
// The gauge only ever falls inside an attempt, so a gauge that RISES is the
// retry, and that is the edge this hangs on.
static void NewAttempt(const char* why)
{
    s_hintedBoost = false;
    Log::Field("FieldNavigation: [SPACE] new attempt (%s)", why);
}

// Active while the scene is running and the skip is not: the catalog goes quiet
// for the duration. Aaron's 14:54:22 log has auto-drive setting off toward
// "Exit to Outer Space 5" -- which is Rinoa, catalogued as a door -- in the
// middle of the attempt he was flying.
static bool Active() { return s_on && !s_skip; }

static bool Here()
{
    const char* why = "";
    return D3Here(SS_ID, "ssspace3", &why);
}

static bool ReadXY(int32_t* x, int32_t* y)
{
    return D3ReadI32(SR_ADDR_X, x) && D3ReadI32(SR_ADDR_Y, y);
}

// The fuel gauge, as a percentage. -1 when it cannot be read, which is treated
// everywhere below as "say nothing" rather than as empty.
static int ReadFuelPct()
{
    int32_t f = 0;
    if (!D3ReadI32(SR_ADDR_FUEL, &f)) return -1;
    if (f > SR_FUEL_FULL * 2 || f < -SR_FUEL_FULL * 2) return -1;   // not this scene yet
    return SrFuelPct((int)f);
}

// Is the boost engaged? Read off var[1034] -- the game's own live step, 8 while
// the boost mask is held and 4 while it is not -- rather than off the keyboard,
// so it is right whatever the key is bound to and right if the game ever stops
// honouring it.
static bool BoostOn()
{
    uint8_t step = 0;
    if (!D3ReadU8(SR_ADDR_STEP, &step)) return false;
    return step >= SR_STEP_VAR_BOOST;
}

// ---------------------------------------------------------------------------
// THE GAME CONTROLS SCREEN
// ---------------------------------------------------------------------------
// The window is 320x224 and the measurer charges about 18 px a line -- the
// Garden battle's six-line box came back 108 px high -- so eleven lines is the
// ceiling and this is nine. Every line is inside BRIEF_COLS (34) so the wrap
// never turns one line into two behind our back. tests/disc3_wiring_compile.cpp
// asserts both, because a box that runs off the bottom of the screen is exactly
// what v0.20.125 had to go back and fix.
// v0.63.2: built rather than fixed, because it names the boost key, and that
// key is whatever the player has bound. The learner's seed for mask 0x0010 is W
// -- measured on Aaron's own keyboard, twice -- so it is never the vague
// version even on the first press of a session.
static const char* SP_SCREEN_FMT =
    "REACHING RINOA\n"
    "%s\n"
    "%s\n"
    "HOLD an arrow down, don't tap.\n"
    "Two at once makes a diagonal.\n"
    "Boost: hold %s.\n"
    "4x speed; burns boost fuel.\n"
    "\"Centred\" means let go.\n"
    "Only the very end counts.\n"
    "Enter start   / repeat   F9 skip";

// Same again for the spoken brief. The first sentence is the only difference.
static const char* SP_SPEECH_PAUSE[2] = {
    "Nothing in the scene moves until you press Enter -- not Rinoa, not the "
    "clock -- so take as long as you like. ",
    "The mission clock is held while this screen is up, but the scene itself "
    "is not, so press Enter as soon as you have this. ",
};

static const char* SP_SPEECH_FMT =
    "Game controls. Reaching Rinoa. %s"
    "The arrow keys steer: hold the direction the mod calls DOWN -- "
    "holding it keeps moving her, tapping it moves her once -- and hold two "
    "arrows together for a diagonal. Hold %s to boost: four times the speed, "
    "and it burns the boost fuel gauge twice as fast. If she is a long way "
    "out, boost -- it is the difference between reaching her and running out "
    "of scene. The mod calls a bearing every "
    "few seconds; when it says centred, let go of the keys, because nothing "
    "moves her but you. Only where she is at the very end counts. Slash "
    "repeats this and reads the fuel. F9 skips the game. Press Enter to start.";

// The screen text, with the boost key resolved. Rebuilt on every open and on
// every slash repeat, so a key learned mid-scene shows up rather than being
// frozen at whatever was known when the box first went up.
// The two lines that describe the pause tell the truth about WHICH pause this
// build got. v0.63.3 had to say "she drifts on" because she did; with the field
// frozen nothing moves at all, and saying so is now accurate rather than the
// over-promise that started this.
static const char* SP_PAUSE_LINES[2][2] = {
    { "Nothing moves until you press",     // the real freeze
      "Enter. Take as long as you like." },
    { "Clock held while you read this,",   // the clock-only fallback
      "but she drifts on: don't linger." },
};

static void BuildScreenText()
{
    GardenBattle::CopyKeyName(s_boostKey, sizeof s_boostKey, SR_MASK_BOOST);
    const int k = FieldPause::Available() ? 0 : 1;
    snprintf(s_screenText, sizeof s_screenText, SP_SCREEN_FMT,
             SP_PAUSE_LINES[k][0], SP_PAUSE_LINES[k][1], s_boostKey);
}

static void SpeakBrief(DWORD now)
{
    char msg[1024];
    snprintf(msg, sizeof msg, SP_SPEECH_FMT,
             SP_SPEECH_PAUSE[FieldPause::Available() ? 0 : 1], s_boostKey);
    D3Say("SPACE", msg, true);
    s_nudgeAt = now ? now : 1;
}

static void OpenScreen(DWORD now, int32_t x, int32_t y)
{
    s_screen   = true;
    s_screenAt = now ? now : 1;
    s_drawnAt  = s_screenAt;      // the field freeze waits SP_DRAW_MS from here
    s_nudgeAt  = s_screenAt;
    s_pinX = x; s_pinY = y;
    BuildScreenText();
    // THE GAME'S OWN WINDOW IS THE ONE THE PLAYER SEES.
    //
    // Aaron: *"I don't think it looks very good if our injected dialogs / text
    // doesn't look the same as the rest of the game."* The way to look exactly
    // like FF8 is not to imitate its font -- it is to use its window, which this
    // has been opening all along. What made that box unusable was that the
    // freeze caught it half-typed; v0.67.0 sets its text speed to zero, so it is
    // complete on the frame it opens (see field_minigame_bgbtl_dialog.inl).
    //
    // The mod's own overlay is now the FALLBACK, not the front-runner. It exists
    // for the build or the moment where the game's window will not open, and
    // showing both would put our box on top of the game's -- which is exactly
    // what the 13:12 frame did, our 372x240 sitting inside the game's 459x320.
    const bool gameBox = GardenBattle::OpenBriefDialog(s_screenText);
    if (!gameBox) FieldOverlay::Show(s_screenText);
    SpeakBrief(now);
    Log::Field("FieldNavigation: [SPACE] game controls open at x=%ld y=%ld "
               "(%s, boost key %s)", (long)x, (long)y,
               FieldPause::Available() ? "the field will freeze in a moment"
                                       : "no field freeze on this build -- "
                                         "holding the clock instead",
               s_boostKey);
}

static void CloseScreen(const char* why)
{
    if (!s_screen) return;
    s_screen = false;
    GardenBattle::CloseBriefDialog();
    FieldOverlay::Hide();
    Log::Field("FieldNavigation: [SPACE] game controls closed (%s)", why);
}

// ---------------------------------------------------------------------------
// THE STEERING CALL
// ---------------------------------------------------------------------------
// SrCall builds the phrase; this adds the one thing a phrase cannot carry --
// whether what he is doing is working. "still" on a repeat that has not closed
// any ground is the sentence Aaron's first attempt needed and never got.
static void CallIt(int32_t x, int32_t y, bool force)
{
    char word[64];
    SrCall((int)x, (int)y, word, sizeof word);

    const int worst = SrWorst((int)x, (int)y, SR_CLAMP_X, SR_CLAMP_Y);
    const int trend = (s_lastWorst < 0) ? 1 : SrTrend(s_lastWorst, worst);
    s_lastWorst = worst;

    char line[96];
    if (strcmp(word, "centred") == 0) {
        snprintf(line, sizeof line, "%scentred",
                 (!force && strcmp(s_lastWord, "centred") == 0) ? "still " : "");
    } else if (!force && trend <= 0 && strcmp(word, s_lastWord) == 0) {
        // Same call as last time and no ground closed: say so.
        snprintf(line, sizeof line, "still %s", word);
    } else {
        snprintf(line, sizeof line, "%s", word);
    }
    strncpy(s_lastWord, word, sizeof(s_lastWord) - 1);
    s_lastWord[sizeof(s_lastWord) - 1] = '\0';
    ScreenReader::Speak(line, true);
    Log::Field("FieldNavigation: [SPACE] x=%ld y=%ld worst=%d trend=%d -> \"%s\"",
               (long)x, (long)y, worst, trend, line);

    // ONE NUDGE TOWARD THE BOOST, AT THE MOMENT IT DECIDES THE SCENE.
    //
    // Aaron's 2026-08-23 attempt 1 was lost 280 units out and still closing:
    // he started at x=2500, which is 52 seconds of unboosted travel at the
    // measured 48 units a second, and the Game Controls screen had already
    // taken part of the scene. Thirteen seconds of boost would have covered it.
    // The brief says the boost exists; a line said WHEN the error is wide is
    // the one that gets used. Once per attempt, queued behind the bearing.
    // NOT `far`. windows.h still defines far, near, pascal and huge as empty
    // macros from the segmented-memory days, so `const int far = ...` reaches
    // MSVC as `const int = ...` -- "error C2513: no variable declared before
    // '='", which is what v0.63.3 shipped. tests/winshim/windows.h and the
    // wiring probe now define them too, so the host gate sees it first.
    const int wideErr = (abs((int)x) > abs((int)y)) ? abs((int)x) : abs((int)y);
    if (!s_hintedBoost && wideErr >= SP_NEAR_BAND && !BoostOn()) {
        s_hintedBoost = true;
        char hint[128];
        snprintf(hint, sizeof hint,
                 "That is a long way. Hold %s to boost -- four times the speed.",
                 s_boostKey[0] ? s_boostKey : "the boost key");
        ScreenReader::Speak(hint, false);
        Log::Field("FieldNavigation: [SPACE] boost hint at %d units out", wideErr);
    }
}

static void Update(bool slash, bool f9)
{
    if (!Here()) {
        if (s_on) {
            CloseScreen("left the field");
            Log::Field("FieldNavigation: [SPACE] left ssspace3");
            Reset();                 // -> Unpause: the byte goes back here too
        }
        return;
    }
    if (!s_on) {
        s_on = true;
        Log::Field("FieldNavigation: [SPACE] entered ssspace3 (id %u)", (unsigned)D3FieldId());
    }

    int32_t x = 0, y = 0;
    if (!ReadXY(&x, &y)) return;
    // Outside the clamp the scene has not started writing yet; saying anything
    // then is the "laguna=216" mistake from the dragon fight in another form.
    if (x > SR_CLAMP_X || x < -SR_CLAMP_X || y > SR_CLAMP_Y || y < -SR_CLAMP_Y) return;

    // ...AND NEITHER HAS IT AT (0,0). v0.64.0 shipped with the clamp as the
    // whole gate, and zero is inside a clamp. The field-variable block is
    // ZEROED ON FIELD LOAD, so the very first readable frame satisfied it and
    // Aaron's 2026-08-23 log shows what followed: "game controls open at x=0
    // y=0 (clock NOT stopped)" on the same second as "entered ssspace3", the
    // field frozen mid-initialisation for forty-four seconds, her position
    // pinned at a zero that was never a position, and a spurious "centred" at
    // the instant Enter let the scene run. It ended well, but freezing
    // field_main while field_main is still LOADING the field is not something
    // to leave to luck.
    //
    // The scene's own init is the gate. rinoa::default 16-17 writes
    // var[1052] = 8000, then 20-126 picks one of eight starting offsets --
    // (-1800,-2000), (3000,3700), (3000,-350), (-2700,200), (900,-43),
    // (-3300,1500), (2500,-800), (1100,2000). Not one of them is (0,0), and the
    // fuel is not 8000 until the scene has started. Both together mean the
    // approach exists to be paused.
    const int fuelNow = ReadFuelPct();
    if (!s_briefed && (x == 0 && y == 0)) return;
    if (!s_briefed && fuelNow < 100) return;

    const DWORD now = GetTickCount();
    if (!s_arrivedAt) s_arrivedAt = now ? now : 1;
    if (FieldDialog::IsDialogOpen()) { s_dlgLast = now; s_sawDlg = true; }

    // Keep the key learner running here too. It watches the same field button
    // word this scene reads, its four candidate masks include 0x0010, and the
    // arrow keys are not in its candidate list -- so steering cannot teach it a
    // wrong name, and the first real press of the boost key confirms the seed
    // instead of leaving the screen to guess.
    GardenBattle::LearnButtons();

    // WHICH PAUSE THIS BUILD GETS, decided once and before anything commits to
    // a strategy. FieldPause::Available() answers it without patching anything:
    // it needs field_main resolved and its entry reading A1 64 4A CE 01.
    const bool canFreeze = FieldPause::Available();

    // AND THE CLOCK IS HELD EITHER WAY. v0.64.0 made this conditional on the
    // freeze being unavailable, on the reasoning that stopping field_main stops
    // the countdown with it. THAT REASONING WAS WRONG, and Aaron's 2026-08-23
    // 22:05 log is the proof: [FIELDPAUSE] ENGAGED at 22:05:07 and the global
    // went 90, 89, 88, 87 ... one a second straight through it, reaching zero at
    // 22:06:35 while the screen was still up. Whatever decrements 0x01CFE92C is
    // not field_main. v0.64.0 only looked right because its arrival gate opened
    // the screen BEFORE rinoa::default reached SETTIMER; v0.64.1 moved the
    // freeze after it and the truth came out.
    //
    // What that cost: the clock hit zero during the read, so the moment Enter
    // let the scripts run again timer0::start0 set var[1037] = 1, director1's
    // control loop exited, and no key moved Rinoa ever again -- x sat at 2500
    // for four minutes of "still well right and down". The scene was lost
    // before he started. Two mechanisms for one job is the right number when
    // one of them provably does not cover the other.
    if (!s_briefed && !s_skip) Hold(true, "game controls");

    // F9 is read FIRST, before the brief wait and before the screen gate. It is
    // the escape hatch, and an escape hatch that only works once the mod has
    // finished talking is not one -- waiting out Squall's monologue is exactly
    // when a player who does not want to fly this scene reaches for it.
    if (f9 && !s_skip) {
        s_skip = true;
        s_briefed = true;
        CloseScreen("skip");
        Unpause("skip");
        D3Say("SPACE", "Skip on. Holding her centred -- the rescue will complete on its own.", true);
        Log::Field("FieldNavigation: [SPACE] skip armed at x=%ld y=%ld", (long)x, (long)y);
    }
    if (s_skip) {
        // THE SKIP. The verdict reads exactly these two variables, once, at the
        // end of the approach. Holding both at zero makes the game's own
        // bytecode take its own win branch -- var[1035]=1, var[256]=2564,
        // MAPJUMPO 877 are all the game's. Nothing is forged and no transition
        // is synthesised. It has to be held every tick because the key handlers
        // keep adding to the same two variables; a one-shot write would be
        // undone by the next frame the player leans on a direction.
        D3WriteI32(SR_ADDR_X, 0);
        D3WriteI32(SR_ADDR_Y, 0);
        return;
    }

    if (!s_briefed) {
        if (canFreeze) {
            // NOTHING TO WAIT FOR. Squall's three lines were only ever a problem
            // because they landed on top of a brief the scene ran through; a
            // frame that never happens cannot open a dialogue. Brief at once,
            // as early as possible, so the fewest frames go by before the field
            // stops -- his first line is two seconds in and SP_DRAW_MS is 200.
            s_briefed = true;
            OpenScreen(now, x, y);
        } else {
            // THE FALLBACK, unchanged from v0.63.3. Quiet means "a dialogue has
            // FINISHED", not "none is open" -- the gap between those two
            // sentences is the whole v0.63.0 failure.
            const bool quiet   = !FieldDialog::IsDialogOpen() &&
                                 (now - s_dlgLast >= SP_DLG_QUIET_MS);
            const bool settled = (now - s_arrivedAt) >= SP_SETTLE_MS;
            const DWORD cap    = CountdownTimer::IsHeldFrozen() ? SP_BRIEF_CAP_HELD_MS
                                                                : SP_BRIEF_CAP_MS;
            const bool tooLong = (now - s_arrivedAt) >= cap;
            if ((quiet && (s_sawDlg || settled)) || tooLong) {
                s_briefed = true;
                OpenScreen(now, x, y);
                if (tooLong && !quiet)
                    Log::Field("FieldNavigation: [SPACE] briefing over the scene's own "
                               "dialogue -- %u ms is as long as the wait can run",
                               (unsigned)cap);
            } else {
                return;   // still Squall's own lines. Say nothing over them.
            }
        }
    }

    // THE PAUSE. Nothing else runs while the box is up: no bearing (two voices
    // at once), and her position is pinned, so an arrow pressed while reading
    // does not fly the ship. The clock is already stopped. Enter is the only
    // ordinary way out -- see SP_SCREEN_CAP_MS for why the backstop is not one.
    if (s_screen) {
        // FREEZE THE FIELD, once the box has had SP_DRAW_MS of frames to be
        // drawn in. Deliberately not on the same tick as the open: window
        // rendering hangs off field_main, so a box opened and frozen together
        // is a box that was never drawn.
        // THE ONE MOMENT A SCREENSHOT OF THE BOX IS POSSIBLE.
        //
        // The 12:39 BAT settled what has been costing builds since v0.65.1: the
        // frozen field does not present. No SwapBuffers means no draw and no
        // capture, so an F11 pressed during the pause is not serviced until the
        // swap AFTER Enter -- by which time the box has been hidden. Both of
        // Aaron's presses that run produced one file, written at 12:39:51.996,
        // the exact second the freeze released.
        //
        // So the shot has to be taken in the window this code already leaves
        // open: between the box going up and the freeze engaging, while frames
        // are still being presented. One request, once per scene, on the next
        // swap. It costs nothing and it is the only way anyone finds out
        // whether the box is on the screen.
        if (!s_shotAsked && s_drawnAt && (now - s_drawnAt) >= SP_SHOT_MS) {
            s_shotAsked = true;
            char base[512];
            snprintf(base, sizeof base, "%s\\overlay_%lu",
                     BattleTTS::GetScreenshotDir(), (unsigned long)(now % 1000000u));
            BattleTTS::RequestScreenshotAsync(base);
            Log::Field("FieldNavigation: [SPACE] overlay self-capture requested at %s.png "
                       "-- taken BEFORE the freeze, because a frozen field presents "
                       "no frames and therefore captures none", base);
        }

        // FREEZE WHEN THE BOX IS FINISHED, NOT WHEN A TIMER SAYS SO.
        //
        // The window is drawn by field_main, so whatever it has managed to type
        // by the moment the byte at 0x00471F70 changes is what stays on the
        // screen for the whole pause. Every screenshot before v0.67.0 caught it
        // mid-word -- "REACHING RIN" in one run, "REACHING RINOA" plus a stray
        // "N" in another. With the text speed at zero it should be complete on
        // the frame it opens, and this waits for the engine's own +0x28 flag to
        // say so rather than trusting that it did. SP_DRAW_MS stays as a floor
        // and SP_BOX_CAP_MS as a ceiling: a box that never reports complete must
        // not hold the scene open for ever.
        const DWORD waited = s_drawnAt ? (now - s_drawnAt) : 0;
        const bool  boxDone = GardenBattle::BriefDialogOpen()
                                ? GardenBattle::BriefDialogComplete() : true;
        if (canFreeze && !s_realPause && s_drawnAt && waited >= SP_DRAW_MS &&
            (boxDone || waited >= SP_BOX_CAP_MS)) {
            s_swapsAtFreeze = FieldOverlay::SwapCount();
            Log::Field("FieldNavigation: [SPACE] freezing after %lu ms -- the game's box "
                       "%s", (unsigned long)waited,
                       boxDone ? "reports its text complete"
                               : "**never reported complete; freezing on the cap, so the "
                                 "box on screen may be part-typed**");
            s_realPause = FieldPause::Engage("game controls");
            if (!s_realPause) {
                // It said it was available and then would not take. Fall back
                // to the clock so the screen is not free-running over the scene.
                Hold(true, "game controls (freeze refused)");
            }
        }

        D3WriteI32(SR_ADDR_X, s_pinX);
        D3WriteI32(SR_ADDR_Y, s_pinY);
        x = s_pinX; y = s_pinY;

        if (slash) {
            BuildScreenText();                      // a key learned since the open
            if (!GardenBattle::OpenBriefDialog(s_screenText))
                FieldOverlay::Show(s_screenText);
            SpeakBrief(now);
        }

        const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        const bool hit   = enter && !s_enterWas;
        s_enterWas = enter;
        if (hit) {
            const int spent = HeldSeconds(now);
            const bool wasReal = s_realPause;
            CloseScreen("Enter");
            Unpause("Enter");
            // WHAT THE FREEZE DOES NOT BUY. Aaron's 2026-08-23 attempt 1: the
            // clock was held for sixty-three seconds, and the scene still ended
            // with FORTY-SEVEN showing on it -- KILLTIMER at 18:34:33 against a
            // 60-second boundary announced at 18:34:20. The mission clock stops;
            // the approach the verdict hangs on does not, because the box has to
            // be DRAWN and window rendering hangs off field_main (the one thing
            // the Garden battle's retired RET-over-field_main pause proved, and
            // the reason it is retired). So the clock now over-reports the
            // remaining scene by roughly what it stood still for, and the honest
            // move is to say so once rather than to let the countdown's own
            // "one minute remaining" imply time that is not there.
            // IS THERE ANYTHING LEFT TO FLY? With the clock held there should
            // always be, but "should" is what v0.64.0 said about the countdown.
            // One check, at one moment, with no false-positive window: if the
            // scene's clock is gone by the time he starts, say so rather than
            // reading bearings into a scene that has already ended.
            if (!CountdownTimer::IsActive()) {
                ScreenReader::Speak("Careful -- the mission clock has already run out. "
                                    "If nothing responds, press F9 or take the retry.", false);
                Log::Field("FieldNavigation: [SPACE] *** the countdown was gone at Enter "
                           "-- this attempt is already over ***");
            }
            if (!wasReal && spent >= 10) {
                char note[160];
                snprintf(note, sizeof note,
                         "The scene ran on for those %d seconds, so the end comes "
                         "sooner than the clock says. Boost if she is far out.",
                         spent);
                ScreenReader::Speak(note, false);
                Log::Field("FieldNavigation: [SPACE] clock was held %d s -- the "
                           "scene did not stop with it", spent);
            }
        } else if (now - s_screenAt >= SP_SCREEN_CAP_MS) {
            CloseScreen("safety backstop");
            Unpause("safety backstop");
        } else {
            // He can sit here as long as he likes, so make sure he always knows
            // what ends it. Not an interrupt: it must not cut the brief in half.
            if (now - s_nudgeAt >= SP_NUDGE_MS) {
                s_nudgeAt = now;
                ScreenReader::Speak("Game controls are open. Press Enter to start, "
                                    "slash to hear them again.", false);
            }
            return;
        }
        s_lastCall = 0; s_lastWorst = -1;
    }

    // ---- the fuel gauge ----------------------------------------------------
    // Aaron: "We need to inform the player when their boost is exhausted... a
    // sighted player can tell by the movement but a blind player will need to be
    // explicitly informed." So the bar on screen gets a voice. Bands only, on
    // the way DOWN only, and QUEUED rather than interrupting -- a bearing is
    // the thing he acts on and must never be cut in half by a fuel report.
    const int fuelPct = fuelNow;
    if (fuelPct >= 0) {
        const int band = SrFuelBand(fuelPct);
        if (band > s_fuelBand) {
            // THE GAUGE ONLY FALLS INSIDE AN ATTEMPT. A rise is the retry.
            // Followed silently -- announcing a refill would be announcing news
            // nobody can act on -- but followed, because v0.63.2 did not, and
            // that latched the tracker at empty for the whole second attempt.
            s_fuelBand = band;
            NewAttempt("the gauge refilled");
        } else if (band == s_fuelBand - 1) {
            s_fuelBand = band;
            ScreenReader::Speak(SrFuelWord(band), false);
            Log::Field("FieldNavigation: [SPACE] fuel %d%% -> band %d \"%s\" "
                       "(boost %s)", fuelPct, band, SrFuelWord(band),
                       BoostOn() ? "ON" : "off");
        } else if (band < s_fuelBand) {
            // MORE THAN ONE BAND IN ONE TICK IS NOT A DRAIN. The bands are 25
            // points apart, the gauge falls at most 8 of 8000 per script frame,
            // and the mod ticks faster than the script -- so a fall this steep
            // is the scene tearing down, which is what said "Boost fuel gauge
            // empty" over the "Try again" prompt at 18:34:47.
            Log::Field("FieldNavigation: [SPACE] fuel %d%% jumped %d bands -- "
                       "resyncing without announcing (the scene is resetting)",
                       fuelPct, s_fuelBand - band);
            s_fuelBand = band;
        }
    }

    // ---- entering or losing the aim box ------------------------------------
    // v0.63.1: no tone. "centred" is the announcement, and Aaron flew the scene
    // to the end on the announcements.
    const bool in = SrHeld((int)x, (int)y);
    if (in != s_wasIn) {
        s_wasIn = in;
        Log::Field("FieldNavigation: [SPACE] %s the box at x=%ld y=%ld",
                   in ? "entered" : "lost", (long)x, (long)y);
        CallIt(x, y, true);
        s_lastCall = now;
        return;
    }

    // Slash is the "tell me everything" key: the bearing first, because that is
    // what he acts on, then the state of the boost behind it.
    if (slash) {
        CallIt(x, y, true);
        s_lastCall = now;
        if (fuelPct >= 0) {
            char line[96];
            snprintf(line, sizeof line, "Boost %s. Fuel %d percent.",
                     BoostOn() ? "on" : "off", fuelPct);
            ScreenReader::Speak(line, false);
        }
        return;
    }

    const int   worstAbs = (abs((int)x) > abs((int)y)) ? abs((int)x) : abs((int)y);
    DWORD period = (worstAbs < SP_NEAR_BAND) ? SP_NEAR_MS : SP_CALL_MS;
    // Parked in the box with nothing moving: slow down, do not stop.
    if (in && x == s_lastX && y == s_lastY) period = SP_HELD_MS;
    s_lastX = x; s_lastY = y;
    if (now - s_lastCall >= period) { s_lastCall = now; CallIt(x, y, false); }
}

} // namespace Space
