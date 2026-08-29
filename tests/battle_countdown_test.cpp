// battle_countdown_test.cpp -- the digit that floats over a battle actor
// (#megaflare, v0.108.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills. The arithmetic ones are checked against the engine's own
// code at 0x0050A500, quoted in the model.
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "battle_countdown_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

// ---------------------------------------------------------------------------
// The sentinel. 0xFBA9 read as int16 is -1111, and it means "draw nothing".
// ---------------------------------------------------------------------------
static void TestSentinel()
{
    CHECK((int)(short)0xFBA9 == BC_NO_TIMER, "the sentinel really is 0xFBA9 as int16");
    CHECK(!BcTimerLive(BC_NO_TIMER), "the sentinel is not live");
    // Kills widening the sentinel test to <= or to "any negative". The engine
    // tests one exact value at 0x0050A504 and draws everything else.
    CHECK(BcTimerLive(-1110), "one above the sentinel IS live");
    CHECK(BcTimerLive(-1112), "one below the sentinel IS live");
    CHECK(BcTimerLive(0),     "zero is live -- a counter at zero is on screen");
    CHECK(BcTimerLive(900),   "a full timer is live");
}

// ---------------------------------------------------------------------------
// (frames + 10) / 30, with negatives clamped to zero FIRST
// ---------------------------------------------------------------------------
static void TestDigit()
{
    // 30 frames a second, and the +10 is the engine rounding to nearest third.
    CHECK(BcDigitFor(0)   == 0, "0 frames shows 0");
    CHECK(BcDigitFor(19)  == 0, "19 frames still shows 0");
    CHECK(BcDigitFor(20)  == 1, "20 frames rounds up to 1");
    CHECK(BcDigitFor(30)  == 1, "one second shows 1");
    CHECK(BcDigitFor(49)  == 1, "49 frames still shows 1");
    CHECK(BcDigitFor(50)  == 2, "50 frames rounds up to 2");
    CHECK(BcDigitFor(270) == 9, "nine seconds shows 9");
    CHECK(BcDigitFor(290) == 10,"ten seconds shows 10 -- two digits, still drawn");
    CHECK(BcDigitFor(900) == 30,"a thirty second timer shows 30");
    // Kills dropping the clamp: without it (-1110 + 10) / 30 would be -36.
    CHECK(BcDigitFor(-1)    == 0, "a negative timer draws as 0");
    CHECK(BcDigitFor(-1110) == 0, "a very negative timer still draws as 0");
    // Kills changing 10 to 15 (round-half) or to 0 (truncate).
    CHECK(BcDigitFor(15) == 0, "15 frames is still 0, not a half-second round");
    CHECK(BcDigitFor(1)  == 0, "1 frame is 0, not 1");
}

// ---------------------------------------------------------------------------
// Only two of the fourteen timer slots are ever drawn
// ---------------------------------------------------------------------------
static void TestDrawnSlots()
{
    CHECK(BcSlotIsDrawn(10), "bit 10 (Doom) is drawn");
    CHECK(BcSlotIsDrawn(12), "bit 12 (Gradual Petrify) is drawn");
    // The neighbours matter: bit 8 is Aura, which this mod already reads and
    // which the engine never renders. An off-by-one here would speak Aura.
    CHECK(!BcSlotIsDrawn(8),  "bit 8 (Aura) is NOT drawn");
    CHECK(!BcSlotIsDrawn(9),  "bit 9 (Curse) is NOT drawn");
    CHECK(!BcSlotIsDrawn(11), "bit 11 (Invincible) is NOT drawn");
    CHECK(!BcSlotIsDrawn(13), "bit 13 (Float) is NOT drawn");
    CHECK(!BcSlotIsDrawn(0),  "bit 0 is NOT drawn");
    CHECK(!BcSlotIsDrawn(-1), "a negative slot is NOT drawn");
    CHECK(!BcSlotIsDrawn(14), "past the end of the array is NOT drawn");
    CHECK(strcmp(BcSlotName(10), "Doom") == 0,            "bit 10 is named Doom");
    CHECK(strcmp(BcSlotName(12), "Gradual Petrify") == 0, "bit 12 is named Gradual Petrify");
}

// ---------------------------------------------------------------------------
// When to speak
// ---------------------------------------------------------------------------
static void TestShouldAnnounce()
{
    // First frame it exists.
    CHECK(BcShouldAnnounce(true, 9, false, 0), "the counter appearing is announced");
    // Every change after that -- once a second, because the value is seconds.
    CHECK(BcShouldAnnounce(true, 8, true, 9),  "counting down is announced");
    CHECK(BcShouldAnnounce(true, 0, true, 1),  "reaching zero is announced");
    // Kills dropping the equality test: this fires every frame, so a missing
    // dedup means thirty utterances a second, each interrupting the last, and
    // the player hears nothing at all.
    CHECK(!BcShouldAnnounce(true, 9, true, 9), "the same number is not repeated");
    // Kills dropping the live test.
    CHECK(!BcShouldAnnounce(false, 9, false, 0), "an absent counter says nothing");
    CHECK(!BcShouldAnnounce(false, 0, true, 1),  "a counter that has gone says nothing");
    // A counter that vanishes and comes back speaks again even at the same
    // number -- it is a new counter.
    CHECK(BcShouldAnnounce(true, 9, false, 9), "a re-appearing counter is announced again");
}

// ---------------------------------------------------------------------------
// The sentence
// ---------------------------------------------------------------------------
static void TestText()
{
    char buf[64];
    BcAnnounceText(buf, sizeof(buf), 5);
    CHECK(strcmp(buf, "Alert, Countdown 5") == 0, "the sentence is Aaron's, verbatim");
    BcAnnounceText(buf, sizeof(buf), 0);
    CHECK(strcmp(buf, "Alert, Countdown 0") == 0, "zero is spoken, not suppressed");
    BcAnnounceText(buf, sizeof(buf), 12);
    CHECK(strcmp(buf, "Alert, Countdown 12") == 0, "two digits are spoken as one number");
    // Kills dropping the negative clamp in the formatter.
    BcAnnounceText(buf, sizeof(buf), -3);
    CHECK(strcmp(buf, "Alert, Countdown 0") == 0, "a negative never reaches the player");
    // A zero-length or null buffer must not be written through.
    char guard[4] = { 'x', 'x', 'x', 'x' };
    BcAnnounceText(guard, 0, 5);
    CHECK(guard[0] == 'x', "a zero-length buffer is left alone");
    BcAnnounceText(nullptr, 16, 5);   // must not crash
}

// ---------------------------------------------------------------------------
// The whole path, frame by frame, as the engine would drive it
// ---------------------------------------------------------------------------
static void TestCountdownRun()
{
    // A ten-second Doom counter ticking down 2 frames per poll (the ticker's
    // normal rate -- 3 hasted, 1 slowed, from 0x00483643).
    bool wasLive = false;
    int  lastDigit = -1;
    int  spoken[64];
    int  n = 0;
    for (int raw = 300; raw >= -2; raw -= 2) {
        bool live = BcTimerLive(raw);
        int digit = BcDigitFor(raw);
        if (BcShouldAnnounce(live, digit, wasLive, lastDigit)) {
            if (n < 64) spoken[n] = digit;
            n++;
        }
        wasLive = live;
        lastDigit = digit;
    }
    CHECK(n == 11, "a ten second counter speaks eleven times: 10 down to 0");
    CHECK(n > 0 && spoken[0] == 10, "it opens at 10");
    CHECK(n == 11 && spoken[10] == 0, "and closes at 0");
    bool descending = true;
    for (int i = 1; i < n && i < 64; i++) if (spoken[i] != spoken[i-1] - 1) descending = false;
    CHECK(descending, "and never skips or repeats a number on the way down");
}

// ---------------------------------------------------------------------------
// The engine's draw gate (v0.110.0). This is the assertion that stops the mod
// shouting "Alert, Countdown 0" fourteen times at the start of every battle.
// ---------------------------------------------------------------------------
static const unsigned FLAG_DOOM    = 0x80000;
static const unsigned FLAG_PETRIFY = 0x2000;

static void TestDrawGate()
{
    // The 2026-08-27 BAT, exactly: battle entry, the timer array not yet
    // written, every slot reading back 0 with no actor flag set at all.
    CHECK(!BcIsDrawn(BcTimerLive(0), true, 0x00000000u, FLAG_DOOM),
          "an uninitialised zero timer with no flag is NOT drawn");
    CHECK(!BcIsDrawn(BcTimerLive(0), true, 0x00000000u, FLAG_PETRIFY),
          "and the same for the petrify slot");
    // A real counter, flag set.
    CHECK(BcIsDrawn(true, true, FLAG_DOOM, FLAG_DOOM),   "a flagged Doom counter is drawn");
    CHECK(BcIsDrawn(true, true, 0x00082000u, FLAG_PETRIFY),
          "both flags set draws the petrify counter too");
    CHECK(BcIsDrawn(true, true, 0x00082000u, FLAG_DOOM),
          "...and the doom counter");
    // Kills using the wrong flag for the slot -- 0x2000 and 0x80000 are
    // different bits of the same word and swapping them is the easy mistake.
    CHECK(!BcIsDrawn(true, true, FLAG_PETRIFY, FLAG_DOOM),
          "the petrify flag does not draw the doom counter");
    CHECK(!BcIsDrawn(true, true, FLAG_DOOM, FLAG_PETRIFY),
          "nor the other way round");
    // Kills dropping the live test, and dropping the read-ok test.
    CHECK(!BcIsDrawn(false, true, FLAG_DOOM, FLAG_DOOM), "a sentinel timer is never drawn");
    CHECK(!BcIsDrawn(true, false, FLAG_DOOM, FLAG_DOOM), "a failed flag read draws nothing");
    // And a zero timer that IS flagged is a genuine counter at zero.
    CHECK(BcIsDrawn(BcTimerLive(0), true, FLAG_DOOM, FLAG_DOOM),
          "a flagged counter at zero is still drawn -- zero is a real tick");
}

int main()
{
    TestDrawGate();
    TestSentinel();
    TestDigit();
    TestDrawnSlots();
    TestShouldAnnounce();
    TestText();
    TestCountdownRun();
    printf("battle_countdown_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
