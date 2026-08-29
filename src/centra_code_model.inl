// centra_code_model.inl -- THE CENTRA RUINS FIVE-DIGIT CODE
//
// v0.115.0 (#centra). Aaron: "You place the eyes in the statue on the top of the
// ruins to get a code, then place the eyes in the statue near the room you have
// to enter and are prompted to enter the code... We need to communicate to
// players how to enter the code and announce the digits as they are entering."
//
// All of this is read out of crtower3's own script (field 285,
// `director0::puteye0`, 773 dwords) and crroof1's (field 280). Nothing is
// inferred from play.
//
// THE ENTRY LOOP, decompiled:
//
//     while (var[1027] != 0) {                  // 1027 = which digit is selected
//       if (var[1027] == 1) {
//         if (button 0x2000) { var[1028]++; if (var[1028] > 9) var[1028] = 0; knum1_disp }
//         if (button 0x8000) { var[1028]--; if (var[1028] < 0) var[1028] = 9; knum1_disp }
//         if (button 0x1000) { var[1027] = 5 }
//         if (button 0x4000) { var[1027] = 2 }
//         if (button 0x0010) { var[1027] = 0 }
//       }
//       if (var[1027] == 2) { ...the same, on var[1029]... }
//       ... 3, 4, 5 ...
//     }
//
// and when the loop ends:
//
//     if (var[1028] == var[364] && var[1029] == var[365] &&
//         var[1030] == var[366] && var[1031] == var[367] &&
//         var[1032] == var[368]) { var[359] |= 32; ...the door opens... }
//
// THE BUTTON MASKS ARE `1 << b` OVER FF8's OWN BUTTON ORDER -- the one this
// project already verified for the `0x05` icon codes (ff8_text_decode.cpp:
// "0=L2 1=R2 2=L1 3=R1 4=Triangle 5=Circle 6=Cross 7=Square 8=Select 9=L3
// 10=R3 11=Start 12=Up 13=Right 14=Down 15=Left"):
//
//     0x1000 = bit 12 = UP     -> previous digit (1 wraps to 5)
//     0x4000 = bit 14 = DOWN   -> next digit     (5 wraps to 1)
//     0x2000 = bit 13 = RIGHT  -> value + 1      (9 wraps to 0)
//     0x8000 = bit 15 = LEFT   -> value - 1      (0 wraps to 9)
//     0x0010 = bit  4 = TRIANGLE -> leave the panel and submit
//
// which is exactly what Aaron described from playing it: "you use up/down to
// move between the first digit, second digit, etc. and left/right actually
// enters a digit". The script agreeing with the player's account of the feel is
// the cross-check that makes the mapping safe to put in a controls screen.

static const uint16_t CC_FIELD_ROOF  = 280;   // crroof1  -- the statue that SHOWS the code
static const uint16_t CC_FIELD_ENTRY = 285;   // crtower3 -- the statue that TAKES it

// The field variable block, the same base every other module in this mod uses.
static const uint32_t CC_VAR_BASE = 0x01CFE9B8u;

static const int CC_VAR_CURSOR   = 1027;      // 0 = not entering, 1..5 = the selected digit
static const int CC_VAR_DIGIT0   = 1028;      // .. 1032, the five entered digits
static const int CC_VAR_TARGET0  = 364;       // .. 368,  the five the roof statue shows
static const int CC_DIGITS       = 5;

static const int CC_CURSOR_IDLE  = 0;

// A digit the script can produce. The wrap keeps it 0..9 and nothing else ever
// reaches the display methods (`zero`..`nine` on no1..no5), so anything outside
// this range means the read was garbage and must not be spoken as a number.
static bool CcDigitValid(int v)      { return v >= 0 && v <= 9; }
static bool CcCursorValid(int c)     { return c >= 0 && c <= CC_DIGITS; }
static bool CcEntryActive(int cursor){ return cursor >= 1 && cursor <= CC_DIGITS; }

// Speak on the first frame the panel opens, and again whenever the selected
// position or the value under it changes. Not otherwise: the script's loop runs
// every frame and re-reads both, so without this the mod would say the same
// digit thirty times a second.
static bool CcShouldAnnounce(bool active, int cursor, int digit,
                             bool wasActive, int lastCursor, int lastDigit)
{
    if (!active) return false;
    if (!wasActive) return true;
    // v0.116.0: A DECREMENT PAST ZERO IS VISIBLE FOR ONE FRAME. The digits are
    // BYTE variables and the script wraps them AFTER the fact -- `var[1028]--`,
    // then `if (CAL 1028 < 0) var[1028] = 9`, with CAL reading the byte as
    // signed. Between those two instructions the byte holds 0xFF, and the
    // 2026-08-27 log caught the mod reading it:
    //
    //     CentraCode: Digit 5 of 5. 0.
    //     CentraCode: Digit 5 of 5.        <-- 0xFF: not a digit, no value
    //     CentraCode: Digit 5 of 5. 9.
    //
    // A position with no value spoken between two that have one is noise at
    // best and, since every line interrupts, it clips the useful one. Hold the
    // announcement; the wrapped value arrives on the very next poll and
    // announces then, because lastDigit was never moved to the garbage.
    if (!CcDigitValid(digit)) return false;
    return cursor != lastCursor || digit != lastDigit;
}

// "Digit 3 of 5. 7." -- position first, because that is what the player has to
// track; the value second, because that is what they just changed. Short, since
// it fires on every press of a direction.
static void CcPositionText(char* buf, size_t n, int cursor, int digit)
{
    if (buf == nullptr || n == 0) return;
    buf[0] = '\0';
    if (!CcEntryActive(cursor)) return;
    if (CcDigitValid(digit)) snprintf(buf, n, "Digit %d of %d. %d.", cursor, CC_DIGITS, digit);
    else                     snprintf(buf, n, "Digit %d of %d.", cursor, CC_DIGITS);
}

// The whole row, for the repeat key and for the moment the panel opens.
static void CcSequenceText(char* buf, size_t n, const char* lead, const int* digits)
{
    if (buf == nullptr || n == 0) return;
    buf[0] = '\0';
    if (digits == nullptr) return;
    for (int i = 0; i < CC_DIGITS; i++) if (!CcDigitValid(digits[i])) return;
    snprintf(buf, n, "%s %d, %d, %d, %d, %d.", lead ? lead : "",
             digits[0], digits[1], digits[2], digits[3], digits[4]);
}

// True when every digit entered matches the one the roof statue is showing --
// the same five comparisons the script makes before it sets var[359] |= 32.
static bool CcCodeMatches(const int* entered, const int* target)
{
    if (entered == nullptr || target == nullptr) return false;
    for (int i = 0; i < CC_DIGITS; i++) {
        if (!CcDigitValid(entered[i]) || !CcDigitValid(target[i])) return false;
        if (entered[i] != target[i]) return false;
    }
    return true;
}

// ============================================================================
// v0.116.0 (#centra): WHEN MAY THE MOD SAY THE CODE OUT LOUD?
// ============================================================================
//
// Aaron, after finishing the ruins: "Just to be sure, you can't press C to hear
// the code if you haven't first heard the code at the top of the Ruins, right?
// Just want to make sure the mod doesn't offer to tell the player the code if
// they haven't actually discovered it."
//
// He was right to check, because v0.115.0 would have. Nothing in the game
// WRITES var[364..368]; they arrive from the savemap already holding the
// answer, so C pressed on the way in would have read out a code the player had
// no business knowing. The mod would have solved the puzzle for them.
//
// The engine does say when the code has been shown, in `crroof1::director0`:
//
//     var[1025] = var[359] & 15
//     if (var[1025] == 12) { OP_0x049(0, var[364]) ... OP_0x049(4, var[368]);
//                            <show the message> }
//
// and the four low bits of var[359] are the four eye sockets, two statues:
//
//     bit 0 (1)  left eye  in the LOWER statue  (crtower3, sets `OR 1`)
//     bit 1 (2)  right eye in the LOWER statue  (crtower3, sets `OR 2`)
//     bit 2 (4)  left eye  in the ROOF  statue  (crroof1,  sets `OR 4`)
//     bit 3 (8)  right eye in the ROOF  statue  (crroof1,  sets `OR 8`)
//
// So `(var[359] & 12) == 12` -- both eyes in the ROOF statue -- is the game's
// own statement that the code is on screen. It cannot be reached from the lower
// statue, which only ever touches bits 0 and 1.
//
// BUT IT IS NOT ENOUGH ON ITS OWN, and this is the part worth writing down.
// The player then TAKES THE EYES BACK OUT to carry them down to the door, which
// clears bits 2 and 3 -- so the test goes false at exactly the moment the code
// is needed. A rule that only asked the live flags would refuse to repeat the
// code at the panel, which is the one place it matters. So the mod latches: the
// first time it sees both roof bits set, the code is his, and stays his.
//
// The latch is session state, not savemap state, and that is a real limit: quit
// and reload between the roof and the door and C says "not yet" until the roof
// is revisited. The alternative is inventing a persistence file keyed to
// nothing, which would leak one playthrough's answer into the next -- a worse
// failure, and a silent one. Refusing too often is recoverable; revealing too
// early is not.
static const int CC_VAR_STATUS      = 359;   // 0x01CFEB1F, the four eye sockets
static const int CC_ROOF_EYES_MASK  = 12;    // bits 2|3 -- both eyes, roof statue

// True the instant the game is showing the code.
static bool CcRoofStatueShowsCode(int status)
{
    return (status & CC_ROOF_EYES_MASK) == CC_ROOF_EYES_MASK;
}

// True when the mod may speak it: showing it now, or having shown it earlier.
static bool CcCodeRevealed(int status, bool latched)
{
    return latched || CcRoofStatueShowsCode(status);
}

// What C says before the code has been earned. It names the next action rather
// than just refusing, because "no" with no direction is the same dead end the
// unnamed terminals were.
static const char* const CC_CODE_UNKNOWN_TEXT =
    "You have not seen the code yet. "
    "Put both eyes into the statue on the roof of the ruins and it will show you five digits.";

// The Game Controls screen, in the shape every other mini-game's uses: short
// lines inside the game window's wrap, the keys last.
static const char* const CC_CONTROLS_BOX =
    "CENTRA RUINS CODE PANEL\n"
    "Up / Down    move between the 5 digits\n"
    "Left / Right change the digit (0-9)\n"
    "Triangle     submit and leave\n"
    "C            say the code again";

// The spoken form of the same thing. The box is a picture; this is what a
// player who cannot see it actually receives, so it is a sentence rather than
// a column, and it names Triangle rather than "the confirm button" because the
// script tests bit 4 and nothing else.
static const char* const CC_CONTROLS_TEXT =
    "Centra Ruins code panel. "
    "Up and Down move between the five digits. "
    "Left and Right change the digit under the cursor, wrapping between 0 and 9. "
    "Triangle leaves the panel and submits the code. "
    "Press C at any time in the ruins to hear the code the roof statue is showing.";

// THE OPENING UTTERANCE IS ONE UTTERANCE, and that is the whole point of this
// function. Every announcement this module makes interrupts, because when a
// player is holding Left the newest digit is the only one worth hearing. On the
// frame the panel opens, `CcShouldAnnounce` is also true -- the cursor has gone
// from "none" to 1 -- so speaking the controls and then the position as two
// calls means the second one cuts the first one off after four words, and the
// player never learns the scheme. Joined here instead, so the controls survive
// and the starting position still arrives.
static void CcOpeningText(char* buf, size_t n, int cursor, int digit)
{
    if (buf == nullptr || n == 0) return;
    buf[0] = '\0';
    char pos[48];
    CcPositionText(pos, sizeof(pos), cursor, digit);
    if (pos[0] == '\0') snprintf(buf, n, "%s", CC_CONTROLS_TEXT);
    else                snprintf(buf, n, "%s %s", CC_CONTROLS_TEXT, pos);
}
