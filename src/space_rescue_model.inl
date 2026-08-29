// space_rescue_model.inl -- the PURE model of the disc-3 space rescue: Squall
// closing on the drifting Rinoa (field `ssspace3`, id 878, "Outer Space 5").
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Compiled standalone by tests/space_rescue_compile.cpp.
//
// ============================================================================
// THE SCENE, READ OUT OF ssspace3 (#111)
// ============================================================================
//
// Aaron: *"The player basically has to adjust the view to keep Rinoa centered
// on the screen until they reach her."* That is exactly what the script does,
// and the whole thing reduces to two signed numbers.
//
// THE ERROR TERM. Two int32 field variables carry Rinoa's offset from the
// centre of the view:
//
//     var 1040  ->  0x01CFEDC8   X   (+ = she is RIGHT of centre)
//     var 1044  ->  0x01CFEDCC   Y   (+ = she is ABOVE centre)
//
// `rinoa::poscheck0` (dwords 673-704) clamps them to +-8100 and +-7300, which
// is the whole range the player ever sees.
//
// THE DIRECTION TO PRESS -- and this is the part that must not be inverted, so
// it is read straight off the four key handlers rather than reasoned about:
//
//     rinoa::keyl  dw 350-353   X = X + step     (holding LEFT  raises X)
//     rinoa::keyr  dw 385-389   X = X - step     (holding RIGHT lowers X)
//     rinoa::keyu  dw 421-425   Y = Y - step     (holding UP    lowers Y)
//     rinoa::keyd  dw 457-461   Y = Y + step     (holding DOWN  raises Y)
//
// So to drive X toward zero you press RIGHT when X is positive and LEFT when it
// is negative; to drive Y toward zero you press UP when Y is positive and DOWN
// when it is negative. In plain terms **you steer toward her**, which is the
// intuitive reading and is also what the bytecode says. The two agree, and the
// bytecode is why this comment can promise it.
//
// THE BUTTONS. `director1::default` reads four masks with BTN_HELD (0x6D, a
// level test -- these are holds, not taps; the loop calls the key handler again
// on every pass while the mask stays down, so TAPPING A DIRECTION MOVES HER
// ONCE) and folds them into var[1024] as a 1..9 direction state:
//
//     0x8000 left   0x2000 right   0x1000 up   0x4000 down
//     0x0010        THE BOOST
//
// The masks are named by the game's own `.sym` method names (rinoa::keyl and
// friends), not by assumption. The mod resolves them to the player's real key
// names through the same learner the Garden battle and the dragon fight use.
//
// ============================================================================
// THE BOOST, READ OUT OF THE SCRIPT (v0.63.2, #111)
// ============================================================================
//
// Aaron: *"There is an option to boost for a limited duration. I am fairly sure
// this is toggled by holding down X, but please confirm in the game exe."*
//
// It is mask 0x0010, and on his machine that is not X. The Garden battle's key
// learner measured every one of these across two runs on his own keyboard --
// mask 16 with W, 32 with D, 64 with X, 128 with A, on the same field button
// word (0x01CE48B0) this scene reads -- so 0x0040 is X and **0x0010 is W**. The
// mod does not hardcode it either way: it names whatever key is bound through
// that learner, which is also what makes the answer survive a remap.
//
// IT IS FOUR TIMES THE SPEED, NOT TWICE. The multiplier is applied in TWO
// places, both gated on the same mask, and reading only one of them undercounts
// it by half:
//
//   director1::default 13-23    boost held ? var[1034] = 8 : var[1034] = 4
//   rinoa::keyl 5-25            boost held ? X += var[1034] * 2
//                                          : X += var[1034]
//
// so a boosted frame moves 16 and an unboosted one moves 4. Every one of the
// eight direction handlers carries the same pair, diagonals included, and each
// plays a different thruster SFX for the two cases (341632 boosted, 42704 not)
// -- which is how a hearing player already knows.
//
// THE FUEL. `rinoa::default` 16-17 sets var[1052] = 8000, and every pass of
// director1's loop in which a direction is held does var[1052] -= var[1034]:
// four a frame normally, eight while boosting. `PSHN_L 0; RDVARSW 1052; op315`
// at the top of the same loop writes it to gauge 0 -- the bar on screen.
//
// AND NOTHING READS IT. Not one script in ssspace1/2/3 tests var[1052], and
// FF8_EN.exe holds no reference to its address; opcode 315 (0x00529BF0) only
// stores the value and calls 0x004B7C90, which clamps the BAR to the gauge's
// min/max and touches nothing else. So the gauge empties and the boost keeps
// working -- the mod reports the gauge, which is the thing a sighted player can
// see, and does not claim a mechanical cutoff the bytecode does not contain.
//
// THE VERDICT. Taken EXACTLY ONCE, in `rinoa::default` dwords 225-243, after
// the approach animation finishes (`0x0F5` at dword 224):
//
//     if (var1040 > -180 && var1040 < 180 &&
//         var1044 > -180 && var1044 < 180)   var[1035] = 1
//
// and then dwords 245-256: `var[1035]==1` -> var[256]=2564, MAPJUMPO 877, the
// reunion. Being centred at any earlier moment earns nothing; being centred at
// that instant is the entire game. **180 is therefore not a tolerance this mod
// chose -- it is the game's own win box**, and the mod's "centred" means
// exactly it.
//
// Failure is "Squall was lost in space...forever." and a Try again / Quit ask
// with unlimited retries, so a missed attempt costs time and not the save.
//
// THE SKIP. The verdict has exactly two inputs. Holding both at zero for the
// frame it is taken makes the game's own bytecode declare the win -- every
// line after dword 243 is the game's, including the MAPJUMPO. Nothing is
// forged, no transition is synthesised (the v0.20.110 crash is the standing
// reminder of why that matters), and because the key handlers keep adding to
// the same variables it has to be a mode held every tick rather than a
// one-shot write.

static const int      SR_FIELD_ID   = 878;          // ssspace3
static const uint32_t SR_ADDR_X     = 0x01CFEDC8u;  // var 1040, int32
static const uint32_t SR_ADDR_Y     = 0x01CFEDCCu;  // var 1044, int32
static const int      SR_WIN_BOX    = 180;          // the game's own |x|,|y| test
// v0.63.0: STEER TO THE MIDDLE OF THE BOX, NOT ITS EDGE.
//
// SrMaskForX used to stop asking for a correction at exactly 180, which parks
// the player on the boundary with no margin at all -- and the verdict is taken
// once, at an instant the player cannot see coming. A 400 ms reaction to the
// "centred" call is already 48 units of overshoot at the unboosted step, and
// the offline sim lands at 182 and loses the scene if the scene turns out to
// drift her sideways at all. So the STEERING deadband is a third of the
// verdict's, and "centred" is not said until the player is inside it. The win
// box is still the game's 180: SrCentred is untouched, because that is the
// thing being predicted, not the thing being asked for.
static const int      SR_AIM_BOX    = 60;
static const int      SR_CLAMP_X    = 8100;
static const int      SR_CLAMP_Y    = 7300;

// THE FUEL GAUGE. var 1052, an int32, set to 8000 by rinoa::default and drained
// by director1. var 1034 is the live step, a byte -- 8 while the boost is held
// and 4 otherwise -- which is how the mod knows the boost is engaged without
// guessing at a keyboard.
static const uint32_t SR_ADDR_FUEL  = 0x01CFEDD4u;  // var 1052, int32
static const uint32_t SR_ADDR_STEP  = 0x01CFEDC2u;  // var 1034, uint8
static const int      SR_FUEL_FULL  = 8000;

// The four D-pad masks, exactly as director1::default tests them.
static const uint16_t SR_MASK_LEFT  = 0x8000;
static const uint16_t SR_MASK_RIGHT = 0x2000;
static const uint16_t SR_MASK_UP    = 0x1000;
static const uint16_t SR_MASK_DOWN  = 0x4000;
static const uint16_t SR_MASK_BOOST = 0x0010;

// Centred means the game's win box, not a nicer number.
static bool SrCentred(int x, int y)
{
    return x > -SR_WIN_BOX && x < SR_WIN_BOX &&
           y > -SR_WIN_BOX && y < SR_WIN_BOX;
}

// Which mask closes the gap on each axis. 0 when that axis is already inside
// the box. Derived from the key handlers' own arithmetic: the mask to press is
// the one whose handler moves the variable TOWARD zero.
static uint16_t SrMaskForX(int x)
{
    if (x >= SR_AIM_BOX)  return SR_MASK_RIGHT;   // keyr subtracts
    if (x <= -SR_AIM_BOX) return SR_MASK_LEFT;    // keyl adds
    return 0;
}
static uint16_t SrMaskForY(int y)
{
    if (y >= SR_AIM_BOX)  return SR_MASK_UP;      // keyu subtracts
    if (y <= -SR_AIM_BOX) return SR_MASK_DOWN;    // keyd adds
    return 0;
}
// Safely inside -- the module's "centred", and what the tone cue fires on.
// Deliberately tighter than the verdict so that hearing it means there is room
// to spare rather than that the next 400 ms could throw it away.
static bool SrHeld(int x, int y)
{
    return x > -SR_AIM_BOX && x < SR_AIM_BOX &&
           y > -SR_AIM_BOX && y < SR_AIM_BOX;
}

// THE STEP. Both key arms read var[1034] and add or subtract it; director1's
// init writes 8 into it when the boost bit (0x0010) is held and 4 when it is
// not, which is the `initVar[0] addr=1034 value=8 / initVar[1] ... value=4`
// pair the DIRECTOR dump prints on entry.
//
// v0.63.1: SR_TICK_HZ WAS WRONG, AND THE GAME SAID SO.
//
// The 30 came from "field scripts run once a frame", which is a reasonable
// thing to believe and is not what happens. Aaron's 2026-08-22 flight logs one
// line a second while he held a single arrow down, and X reads
//
//     896 -> 848 -> 800 -> 752
//
// at 16:12:07, :08, :09, :10 -- FORTY-EIGHT units a second, not a hundred and
// twenty. So the arm fires about twelve times a second, and the sim was flying
// a ship two and a half times faster than the real one. Nothing the player
// hears depends on this number; the OFFLINE PROOF does, which is worse, because
// a sim that lands every start in half the real time will call a scene winnable
// that is not. At 48 units/s a far corner is 8100/48 = 169 seconds away and the
// clock gives ninety: the corners are unreachable IN THE GAME, and tests/
// space_sim_test.cpp now says so out loud rather than passing them by accident.
static const int SR_STEP       = 4;
// The BOOSTED step: var[1034] is 8 while the boost is held, and the key handler
// then multiplies it by 2 again. Sixteen, not eight -- see the note above.
static const int SR_STEP_VAR_BOOST = 8;    // what director1 writes to var[1034]
static const int SR_STEP_BOOST     = SR_STEP_VAR_BOOST * 2;   // what actually moves
static const int SR_TICK_HZ    = 12;    // MEASURED, 2026-08-22. See above.
static const int SR_UNITS_PER_SEC = SR_STEP * SR_TICK_HZ;   // 48

// How far out, in words.
//
// v0.63.0: the old bands were nearly useless where it matters. The win box is
// 180 out of a +-8100 range -- 2.2% of the axis -- so the last few hundred
// units are the entire game, and the old table said nothing at all between 600
// and 2700 ("" for a < clamp/3) while lumping everything under 600 into one
// word. Aaron's first attempt sat at x=2500 for fifty-three seconds hearing
// "right", with no way to tell whether it was 2500 or 250.
//
// The bands are now geometric and fine near zero, because that is where the
// player needs resolution:
//     < 180        centred (the game's own box)
//     < 400        "a hair "       one second of holding
//     < 900        "just "         a few seconds
//     < 2200       ""              plain direction
//     < clamp*2/3  "well "
//     else         "a long way "
static const char* SrBand(int v, int clamp)
{
    const int a = v < 0 ? -v : v;
    if (a < SR_AIM_BOX)      return "";
    if (a < 400)             return "a hair ";
    if (a < 900)             return "just ";
    if (a < 2200)            return "";
    if (a < (clamp * 2) / 3) return "well ";
    return "a long way ";
}

// ============================================================================
// THE ERROR, NORMALISED
// ============================================================================
//
// v0.63.0 put a radar under this scene -- a beep whose pitch rose and whose gap
// shortened as she centred. It worked, and Aaron threw it out after the first
// flight he won with it:
//
//     "Let's get rid of the beep sound effect. It is extremely distracting and
//      the TTS announcement I think is sufficient."
//
// He flew the approach to the end on the words alone, so the words are the
// evidence, and a cue that talks over the thing the player is steering on is a
// cost with the benefit already paid. SrRadarHz and SrRadarGapMs are gone.
//
// SrRadarT stays, because it was never the beep: it is the error normalised per
// axis against its own clamp, WORSE AXIS WINS -- the axis the verdict will fail
// on -- and it is what SrWorst and therefore the "still" trend are built from.
// 0 at the very edge, 1000 at dead centre, in thousandths so the model stays
// integer and the test can assert exact values.
static int SrRadarT(int x, int y, int clampX, int clampY)
{
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int tx = (ax >= clampX) ? 0 : 1000 - (ax * 1000) / clampX;
    int ty = (ay >= clampY) ? 0 : 1000 - (ay * 1000) / clampY;
    return tx < ty ? tx : ty;      // the worse axis wins
}
// Is the player's steering working? Compares the worse-axis error now against
// the same measure a moment ago. The threshold is one tick of the unboosted
// step, so noise in the scene's own drift cannot read as progress.
//   +1 closing   0 not moving   -1 drifting away
static int SrTrend(int prevWorst, int nowWorst)
{
    const int d = prevWorst - nowWorst;
    if (d >  SR_STEP) return  1;
    if (d < -SR_STEP) return -1;
    return 0;
}
// ---------------------------------------------------------------------------
// THE FUEL, IN WORDS
// ---------------------------------------------------------------------------
// Aaron: *"We need to inform the player when their boost is exhausted. I believe
// a sighted player can tell by the movement but a blind player will need to be
// explicitly informed."*
//
// The bands are announced on the way DOWN only. A gauge that can only fall does
// not need hysteresis, and announcing a rise would mean announcing a rise that
// cannot happen.
static const int SR_FUEL_HALF_PCT = 50;
static const int SR_FUEL_LOW_PCT  = 25;

static int SrFuelPct(int fuel)
{
    if (fuel <= 0) return 0;
    if (fuel >= SR_FUEL_FULL) return 100;
    return (fuel * 100) / SR_FUEL_FULL;
}
// 3 full, 2 half, 1 low, 0 empty. Lower is worse, so a band that DROPS is news.
static int SrFuelBand(int pct)
{
    if (pct <= 0)                 return 0;
    if (pct <= SR_FUEL_LOW_PCT)   return 1;
    if (pct <= SR_FUEL_HALF_PCT)  return 2;
    return 3;
}
// The empty line names the GAUGE, not a cutoff: nothing in the game reads
// var[1052], so the boost goes on working after the bar bottoms out. Saying
// "boost gone" would be inventing a rule the bytecode does not have.
static const char* SrFuelWord(int band)
{
    switch (band) {
        case 0:  return "Boost fuel gauge empty.";
        case 1:  return "Boost fuel low.";
        case 2:  return "Boost fuel half.";
        default: return "";
    }
}

// The worse-axis error, normalised the same way the radar normalises it, so
// trend and tone always agree about which axis is in trouble.
static int SrWorst(int x, int y, int clampX, int clampY)
{
    return 1000 - SrRadarT(x, y, clampX, clampY);
}

// ============================================================================
// THE SPOKEN CALL (v0.63.0 -- moved here from field_disc3_space.inl)
// ============================================================================
//
// It lives in the model so tests/space_sim_test.cpp can close the loop: the
// simulated player is given ONLY these words and has to fly the approach on
// them. A steering instruction that cannot be flown is not an instruction, and
// the only way to find that out without asking Aaron to sit through the
// eight-minute scene again is to fly it here.
//
// Two axes, worse first, so the short phrase carries the most useful
// correction. The direction words are fixed: every direction in this mod is
// the arrow keys, which is what auto-drive injects and what the GPS and the
// catalog already say.
static const char* SrWordFor(uint16_t mask)
{
    return (mask == SR_MASK_LEFT)  ? "left"
         : (mask == SR_MASK_RIGHT) ? "right"
         : (mask == SR_MASK_UP)    ? "up"
         : (mask == SR_MASK_DOWN)  ? "down" : "";
}
static void SrCall(int x, int y, char* out, size_t cap)
{
    if (!out || cap == 0) return;
    const uint16_t mx = SrMaskForX(x);
    const uint16_t my = SrMaskForY(y);
    if (!mx && !my) { snprintf(out, cap, "centred"); return; }
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    const bool xFirst = (ax >= ay);
    const uint16_t first  = xFirst ? mx : my;
    const uint16_t second = xFirst ? my : mx;
    const int      fv     = xFirst ? x : y;
    const int      clamp  = xFirst ? SR_CLAMP_X : SR_CLAMP_Y;
    if (second) snprintf(out, cap, "%s%s and %s", SrBand(fv, clamp),
                         SrWordFor(first), SrWordFor(second));
    else        snprintf(out, cap, "%s%s", SrBand(fv, clamp), SrWordFor(first));
}

// The mask a listener would hold on hearing that call. This is the model's own
// statement of what it just asked for, and the simulator plays it back --
// which is what makes the loop closed rather than two restatements of one
// belief. "centred" asks for nothing: hands off, and the error stays where it
// is, because nothing in this scene moves it except the player.
static uint16_t SrMaskForCall(const char* words)
{
    uint16_t m = 0;
    if (!words) return 0;
    if (strstr(words, "left"))  m |= SR_MASK_LEFT;
    if (strstr(words, "right")) m |= SR_MASK_RIGHT;
    if (strstr(words, "up"))    m |= SR_MASK_UP;
    if (strstr(words, "down"))  m |= SR_MASK_DOWN;
    return m;
}

// One frame of the scene, as the key handlers write it: the held mask moves
// each axis by the step, and nothing else does. rinoa::keyl ADDs to X and
// keyr SUBs; keyu SUBs from Y and keyd ADDs.
static void SrStepFrame(int* x, int* y, uint16_t held, int step)
{
    if (!x || !y) return;
    if (held & SR_MASK_LEFT)  *x += step;
    if (held & SR_MASK_RIGHT) *x -= step;
    if (held & SR_MASK_UP)    *y -= step;
    if (held & SR_MASK_DOWN)  *y += step;
    if (*x >  SR_CLAMP_X) *x =  SR_CLAMP_X;
    if (*x < -SR_CLAMP_X) *x = -SR_CLAMP_X;
    if (*y >  SR_CLAMP_Y) *y =  SR_CLAMP_Y;
    if (*y < -SR_CLAMP_Y) *y = -SR_CLAMP_Y;
}
