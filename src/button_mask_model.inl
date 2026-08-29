// button_mask_model.inl -- SAYING WHICH BUTTON THE PLAYER HAS TO PRESS
//
// v0.120.0 (#centra). Aaron, on the Centra tower: auto-drive walked him to the
// ladder and then sat there. The 2026-08-28 log, at 19:36:54 -- parked 15 units
// from the ladder line, still steering at a point 300 units past it,
// `no-progress stuck`, `Cancelled`.
//
// Two things were wrong and this file is the first.
//
// A LADDER IS NOT A DOORWAY. The drive aims 300 units BEYOND a trigger line, on
// purpose, so that walking carries the player across it and the engine fires
// the transition (v0.15.9.2.14, "stopping 300 units short of an exit means the
// player has to walk through manually"). That is right for a doorway. A Centra
// ladder's `touch` method opens
//
//     PSHN_L 192 ; BTNTEST ; PSHL 0 ; PSHN_L 1 ; OPER == ; JPF
//
// and does nothing whatsoever until a button is held. There is no walkmesh past
// it to walk onto, so the drive pushes at a point in mid-air for ever.
//
// BTNTEST IS OPCODE 0x6D, read out of FF8_EN.exe rather than guessed: handler
// 0x0051DA50 pops the mask, ANDs it with the live pad word at [0x01CE48B0] and
// leaves 1 or 0 in the script's local 0 -- which is exactly what the `PSHL 0;
// PSHN_L 1; OPER ==` that always follows it reads back.
//
// THE BIT ORDER IS THE PROJECT'S OWN, AND THE DISC CONFIRMS IT INDEPENDENTLY.
// v0.67.3 established `0=L2 1=R2 2=L1 3=R1 4=Triangle 5=Circle 6=Cross 7=Square
// 8=Select 9=L3 10=R3 11=Start 12=Up 13=Right 14=Down 15=Left` from the 0x05
// icon codes. Every BTNTEST mask on the disc reads as a sentence under it:
//
//     0x00C0  x142   Cross|Square      <- the action pair: ladders, switches
//     0x0080  x103   Square
//     0x0010  x76    Triangle          <- the menu button
//     0x0040  x27    Cross
//     0x1000  x18    Up      0x2000 x16 Right   0x4000 x17 Down   0x8000 x14 Left
//     0xFFFF  x16    any button        <- skip-the-cutscene
//     0x0020  x13    Circle            0x0100 x9 Select   0x0800 x1 Start
//
// Under the raw PlayStation pad order (Select first) the commonest mask in the
// game would be "Left or L2", which is not a thing any script would wait on.
// Two derivations, two decades apart, same answer.
//
// WHAT TO SAY. "Press Cross" is useless to someone playing on a keyboard, and
// this mod already knows better: FF8TextDecode resolves a button index through
// the engine's own keymap at 0x01CD0208 to the key actually bound to it, which
// is how the `0x05` icon codes in dialogue speak as real keys. So the phrase
// names the KEY when the engine gives one and falls back to the pad name only
// when it does not. The mask usually names two buttons because the game accepts
// either; naming both would double the length of a line the player hears every
// time they arrive somewhere, so it names the first that resolves.

#include <cstring>


// ============================================================================
// v0.122.0 (#centra): NEAR AN END IS NOT ON THE LINE
// ============================================================================
//
// v0.120.0 aimed a button-line drive at the nearest point ON the segment and
// arrived within 60 units of it. On crroof1 that arrived four times in two
// minutes and the player never once got up the ladder -- the 2026-08-28 log has
// him at (1050,-753) against a line running (862,-952) to (971,-839), which is
// 117 units from the near endpoint and, projected onto the line, **t = 1.74**:
// three quarters of the segment's own length PAST its far end. He was up the
// ramp from the ladder, not beside it, and the engine's touch zone is the
// segment. Pressing X there does nothing, which is what the log shows -- four
// arrivals, four walks away, never once into crroof1's upper camera zone.
//
// Clamping the aim point to the endpoint was right; treating "60 units from the
// endpoint" as "on the ladder" was not. The projection parameter already
// distinguishes them, and this project already has the margin for it: v0.18.3.304
// bounds the crossing test to a foot inside the segment with a 15% end margin,
// after a nearly-Y-parallel line's infinite extension announced an arrival 960
// units early. Same geometry, same margin, reused rather than re-invented.
//
// A drive that is not yet alongside simply keeps going: the aim point IS the
// endpoint, so walking to it brings t back to 1.0 and inside the margin. The
// failure mode of being too strict is a drive that runs its clock out and says
// so; the failure mode of being too loose is the silent one Aaron just spent
// two minutes on.
static const float BTN_ALONGSIDE_MARGIN = 0.15f;

// Where the player's foot falls along the segment: 0 at (x1,y1), 1 at (x2,y2).
// A degenerate segment answers 0.5, which is alongside -- a zero-length line has
// no "past the end".
static float BtnLineParam(float px, float py,
                          float x1, float y1, float x2, float y2)
{
    const float dx = x2 - x1, dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    if (len2 <= 1.0f) return 0.5f;
    return ((px - x1) * dx + (py - y1) * dy) / len2;
}

static bool BtnLineIsAlongside(float t)
{
    return t >= -BTN_ALONGSIDE_MARGIN && t <= 1.0f + BTN_ALONGSIDE_MARGIN;
}

// ============================================================================
// v0.130.0 (#centra): A SIDE-FLIP IS NOT AN ARRIVAL, AND THE PARAMETER ALONE
//                     CANNOT SAY IT IS
// ============================================================================
// Aaron: "auto-drive is unreliable and doesn't always take me to the ladder."
// The 17:58:25 log is the proof. The drive announced "Ladder Down. Press X to
// use it" while the player stood at (175,-329) -- and crroof1's `lad1` runs
// (444,-509) to (505,-385). That is **320 units** from the segment. He pressed
// X, nothing happened, and the ladder looked broken again.
//
// The arrival came from the CROSSING path: the player's side of the line's
// INFINITE extension flipped, and v0.18.3.304 bounded that with the projection
// parameter (t within the segment, 15% margin). t was 0.76 -- perfectly inside
// -- because the projection parameter says WHERE ALONG the line the player's
// foot falls and says nothing whatever about how far off to the side he is.
// Walking parallel to a line at three hundred units' distance sweeps t from 0
// to 1 without ever approaching it.
//
// So the crossing path gets the test it was always missing: the perpendicular
// distance to the SEGMENT. Both arrival paths now agree on both questions --
// alongside, and near.
static float BtnPointSegDistSq(float px, float py,
                               float x1, float y1, float x2, float y2)
{
    const float dx = x2 - x1, dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    float t = 0.5f;
    if (len2 > 1.0f) {
        t = ((px - x1) * dx + (py - y1) * dy) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    } else {
        t = 0.0f;
    }
    const float ax = x1 + dx * t - px;
    const float ay = y1 + dy * t - py;
    return ax * ax + ay * ay;
}

// AND A BUTTON LINE WANTS THE PLAYER ON IT, NOT PAST IT. The ±15% margin exists
// so a drive that is nearly there is not told to keep walking forever; it was
// never meant to describe where the engine will accept the button. The 18:15
// and 18:21 runs both announced at t=1.02 and t=1.03 -- off the end -- and both
// presses did nothing. Arrival now needs the foot inside the segment proper.
// Being too strict is the safe failure: the aim point is the clamped segment
// point, so a drive that is not yet inside simply keeps walking and gets there.
static bool BtnLineArrivalOk(float t, float distSqToSeg, float arriveDist)
{
    if (t < 0.0f || t > 1.0f) return false;
    return distSqToSeg <= arriveDist * arriveDist;
}

static const int BTN_MASK_ANY = 0xFFFF;   // "press anything" -- cutscene skips

// The verified order. Index is the bit; the name is what the pad calls it.
static const char* const BTN_PAD_NAMES[16] = {
    "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square",
    "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left"
};

static const char* BtnPadName(int button)
{
    if (button < 0 || button > 15) return nullptr;
    return BTN_PAD_NAMES[button];
}

// A mask of every button is "press anything to continue" -- a cutscene skip,
// not an action the player has to be told about.
static bool BtnMaskIsAnyButton(uint16_t mask)
{
    return mask == (uint16_t)BTN_MASK_ANY;
}

// True when the mask names an action worth announcing: at least one button, and
// not the everything-mask.
static bool BtnMaskIsActionable(uint16_t mask)
{
    return mask != 0 && !BtnMaskIsAnyButton(mask);
}

// The nth set bit (n from 0), or -1. Callers walk this to find the first button
// the engine can give a key name for, rather than assuming the lowest one is
// bound.
static int BtnMaskNthButton(uint16_t mask, int n)
{
    if (n < 0) return -1;
    for (int b = 0; b < 16; b++) {
        if ((mask >> b) & 1) {
            if (n == 0) return b;
            n--;
        }
    }
    return -1;
}

static int BtnMaskCount(uint16_t mask)
{
    int c = 0;
    for (int b = 0; b < 16; b++) if ((mask >> b) & 1) c++;
    return c;
}

// "Ladder Up. Press Enter to use it." -- or, with nothing bound to any of the
// buttons, "Ladder Up. Press the Cross button to use it."
//
// `keyName` is what the engine's keymap resolved (nullptr/empty if it could
// not); `padName` is the fallback. With neither, the label is spoken alone
// rather than with a sentence that names nothing.
//
// v0.121.0 (#centra): `leadsTo` NAMES THE THING THE PLAYER ACTUALLY ASKED FOR
// when this is not it. On crtower2 he selected "Left Ladder Down", which is on
// the upper landing, so the island-bridge search sent him to "Left Ladder Up"
// -- correctly -- and the 2026-08-28 log shows the mod then announcing "Left
// Ladder Up. Press X to use it." with no hint of why. Being walked somewhere
// you did not ask for, and told so in a confident voice, is the kind of thing
// that makes a blind player stop trusting the catalog. One clause fixes it.
static void BtnPressText(char* buf, size_t n, const char* label,
                         const char* keyName, const char* padName,
                         const char* leadsTo)
{
    if (buf == nullptr || n == 0) return;
    buf[0] = '\0';
    const char* lab = (label && label[0]) ? label : "";
    if (keyName && keyName[0]) {
        if (lab[0]) snprintf(buf, n, "%s. Press %s to use it.", lab, keyName);
        else        snprintf(buf, n, "Press %s to use it.", keyName);
    } else if (padName && padName[0]) {
        if (lab[0]) snprintf(buf, n, "%s. Press the %s button to use it.", lab, padName);
        else        snprintf(buf, n, "Press the %s button to use it.", padName);
    } else if (lab[0]) {
        snprintf(buf, n, "%s.", lab);
    }
    // The tail only goes on a sentence that exists, and only when it names
    // something OTHER than what was just announced -- a bridge that happens to
    // be the target itself must not say "that is the way to" itself.
    if (buf[0] == '\0') return;
    if (leadsTo == nullptr || leadsTo[0] == '\0') return;
    if (lab[0] && strcmp(lab, leadsTo) == 0) return;
    const size_t used = strlen(buf);
    snprintf(buf + used, (used < n) ? n - used : 0, " That is the way to %s.", leadsTo);
}
