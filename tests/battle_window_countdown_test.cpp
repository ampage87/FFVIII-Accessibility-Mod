// battle_window_countdown_test.cpp -- a number, alone, in a battle text box
// (#megaflare, v0.109.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills. The last block pins the two countdown paths -- the text box
// here and the status counter in battle_countdown_model.inl -- to ONE sentence,
// so a future edit to either cannot leave the player hearing two different
// phrasings for the same idea.
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "battle_window_countdown_model.inl"
#include "battle_countdown_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

static int RawOf(const char* bytes, size_t n)
{
    return BwcFromRawBytes((const unsigned char*)bytes, n);
}

// ---------------------------------------------------------------------------
// Shape 1: the decoder already got there
// ---------------------------------------------------------------------------
static void TestFromDecoded()
{
    CHECK(BwcFromDecoded("5")   == 5,   "\"5\" is five");
    CHECK(BwcFromDecoded("0")   == 0,   "\"0\" is zero, not \"no countdown\"");
    CHECK(BwcFromDecoded("9")   == 9,   "\"9\" is nine");
    CHECK(BwcFromDecoded("12")  == 12,  "\"12\" is twelve");
    CHECK(BwcFromDecoded("100") == 100, "three digits still parse");
    // Kills raising the digit cap: a long run of digits is not a countdown, and
    // reading one out at a boss is worse than silence.
    CHECK(BwcFromDecoded("1000") == BWC_NONE, "four digits is not a countdown");
    // Kills relaxing the all-digits test. "Mega Flare" and "Draw" go past this
    // function every battle and must not be heard as numbers.
    CHECK(BwcFromDecoded("Mega Flare") == BWC_NONE, "an ability name is not a countdown");
    CHECK(BwcFromDecoded("Draw")       == BWC_NONE, "\"Draw\" is not a countdown");
    CHECK(BwcFromDecoded("5 ")  == BWC_NONE, "a trailing space in DECODED text disqualifies");
    CHECK(BwcFromDecoded("HP 5")== BWC_NONE, "a number inside a sentence is not a countdown");
    CHECK(BwcFromDecoded("")    == BWC_NONE, "the empty string is not a countdown");
    CHECK(BwcFromDecoded(nullptr) == BWC_NONE, "null is not a countdown");
    // '/' is '0'-1 and ':' is '9'+1 -- an off-by-one on either bound dies here.
    CHECK(BwcFromDecoded("/") == BWC_NONE, "the character below '0' is not a digit");
    CHECK(BwcFromDecoded(":") == BWC_NONE, "the character above '9' is not a digit");
}

// ---------------------------------------------------------------------------
// Shape 2: the raw FF8 bytes. '0'..'9' are 0x21..0x2A, ' ' is 0x20.
// ---------------------------------------------------------------------------
static void TestFromRaw()
{
    CHECK(RawOf("\x26", 1) == 5,  "raw 0x26 is '5'");
    CHECK(RawOf("\x21", 1) == 0,  "raw 0x21 is '0'");
    CHECK(RawOf("\x2A", 1) == 9,  "raw 0x2A is '9'");
    CHECK(RawOf("\x22\x23", 2) == 12, "raw 0x22 0x23 is \"12\"");
    // FF8 pads its window strings -- that padding is what hid the v0.106.0
    // off-by-one -- so spaces around the number must not disqualify it.
    CHECK(RawOf("\x20\x26\x20", 3) == 5, "spaces around the digit are ignored");
    CHECK(RawOf("\x26\x00\x41\x42", 4) == 5, "the terminator ends the scan");
    // Kills widening the glyph range by one at either end: 0x20 is space
    // (allowed, contributes nothing) and 0x2B is '%'.
    CHECK(RawOf("\x2B", 1) == BWC_NONE, "0x2B ('%') is not a digit");
    CHECK(RawOf("\x1F", 1) == BWC_NONE, "0x1F is below the printable range");
    CHECK(RawOf("\x20\x20", 2) == BWC_NONE, "spaces alone are not a number");
    CHECK(RawOf("", 0) == BWC_NONE, "no bytes is not a number");
    CHECK(BwcFromRawBytes(nullptr, 4) == BWC_NONE, "null bytes is not a number");
    CHECK(RawOf("\x26\x45", 2) == BWC_NONE, "a digit followed by a letter is not a countdown");
    // The control codes the decoder emits nothing for. If the box holds only
    // these, the digits are NOT in the message -- the engine substitutes them
    // -- and guessing would speak a wrong number at a boss.
    CHECK(RawOf("\x0A\x24", 2) == BWC_NONE, "a bare 0x0A value insert yields no number");
    CHECK(RawOf("\x04\x24", 2) == BWC_NONE, "a bare 0x04 numeric insert yields no number");
    CHECK(RawOf("\x22\x23\x24\x25", 4) == BWC_NONE, "four raw digits is not a countdown");
}

// ---------------------------------------------------------------------------
// Which source wins
// ---------------------------------------------------------------------------
static void TestCombined()
{
    const unsigned char five[] = { 0x26, 0x00 };
    const unsigned char name[] = { 0x51, 0x63, 0x65, 0x5F, 0x00 };  // "Mega"
    // The decoded string is authoritative when it produced a number: the
    // decoder resolves everything the engine would have.
    CHECK(BwcCountdownValue("7", five, 2) == 7, "the decoded number wins over the raw bytes");
    // Falls back to the raw bytes when the decode came out empty -- the case
    // that produced ten silent seconds in the Bahamut log.
    CHECK(BwcCountdownValue("", five, 2) == 5, "an empty decode falls back to the raw bytes");
    CHECK(BwcCountdownValue(nullptr, five, 2) == 5, "so does a null decode");
    // And neither source inventing a number out of an ability name.
    CHECK(BwcCountdownValue("Mega Flare", name, 5) == BWC_NONE, "an ability name yields nothing");
    CHECK(BwcCountdownValue("", name, 5) == BWC_NONE, "nor does its raw form");
}

// ---------------------------------------------------------------------------
// When to speak
// ---------------------------------------------------------------------------
static void TestShouldAnnounce()
{
    CHECK(BwcShouldAnnounce(9, BWC_NONE, 0), "the first number is announced");
    CHECK(BwcShouldAnnounce(8, 9, 0),        "a changed number is announced at once");
    CHECK(BwcShouldAnnounce(0, 1, 0),        "reaching zero is announced");
    // Kills dropping the repeat guard: the window poll fires on a hash change,
    // and a redraw can produce one without the text changing.
    CHECK(!BwcShouldAnnounce(9, 9, 0),    "the same number is not repeated immediately");
    CHECK(!BwcShouldAnnounce(9, 9, BWC_REPEAT_MS - 1), "nor just under the window");
    // ...but a countdown that genuinely stalls on a number still ticks along
    // rather than going silent forever.
    CHECK(BwcShouldAnnounce(9, 9, BWC_REPEAT_MS), "the same number speaks again after the window");
    CHECK(!BwcShouldAnnounce(BWC_NONE, 5, 99999), "a box with no number says nothing");
}

// ---------------------------------------------------------------------------
// One sentence, two paths
// ---------------------------------------------------------------------------
static void TestSentence()
{
    char a[64], b[64];
    for (int v = 0; v <= 30; v++) {
        BwcAnnounceText(a, sizeof(a), v);
        BcAnnounceText (b, sizeof(b), v);
        if (strcmp(a, b) != 0) {
            printf("FAIL: the two countdown paths disagree at %d: \"%s\" vs \"%s\"\n", v, a, b);
            g_fail++;
            break;
        }
    }
    BwcAnnounceText(a, sizeof(a), 5);
    CHECK(strcmp(a, "Alert, Countdown 5") == 0, "the sentence is Aaron's, verbatim");
    BwcAnnounceText(a, sizeof(a), 0);
    CHECK(strcmp(a, "Alert, Countdown 0") == 0, "zero is spoken, not suppressed");
    BwcAnnounceText(a, sizeof(a), -2);
    CHECK(strcmp(a, "Alert, Countdown 0") == 0, "a negative never reaches the player");
    char guard[4] = { 'x', 'x', 'x', 'x' };
    BwcAnnounceText(guard, 0, 5);
    CHECK(guard[0] == 'x', "a zero-length buffer is left alone");
    BwcAnnounceText(nullptr, 16, 5);   // must not crash
}

int main()
{
    TestFromDecoded();
    TestFromRaw();
    TestCombined();
    TestShouldAnnounce();
    TestSentence();
    printf("battle_window_countdown_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
