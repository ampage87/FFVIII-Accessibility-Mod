// dialog_short_text_test.cpp -- the length gate, and what it is allowed to
// throw away (#megaflare, v0.107.0).
//
// Every assertion below was written against a mutant; the note on each says
// which change it kills.
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "dialog_short_text_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

static const int MIN_LEN = 3;   // MIN_TEXT_LENGTH, as field_dialog_state.inl has it
static const unsigned MODE_FIELD = 1u;

static bool Gate(const char* s, unsigned mode)
{
    return DlgTextPassesLengthGate(s, s ? strlen(s) : 0u, mode, MIN_LEN);
}

// ---------------------------------------------------------------------------
// The old rule, unchanged, everywhere
// ---------------------------------------------------------------------------
static void TestOldRuleSurvives()
{
    CHECK(Gate("Draw", MODE_FIELD),            "a normal field line passes");
    CHECK(Gate("Mega Flare", DLG_MODE_BATTLE), "a normal battle line passes");
    CHECK(Gate("abc", MODE_FIELD),             "exactly minLen passes");
    // Kills `>` for `>=` in the length test.
    CHECK(!Gate("ab", MODE_FIELD),             "one under minLen is rejected");
    // The two artefacts the gate was written for, named in its own comment.
    CHECK(!Gate("',", MODE_FIELD),  "the ',' artefact is still rejected in the field");
    CHECK(!Gate("C0", MODE_FIELD),  "the 'C0' artefact is still rejected in the field");
    CHECK(!Gate("C0", DLG_MODE_BATTLE), "and in battle too -- it is not all digits");
}

// ---------------------------------------------------------------------------
// The new rule: digits, in battle, at any length
// ---------------------------------------------------------------------------
static void TestBattleDigits()
{
    CHECK(Gate("5", DLG_MODE_BATTLE),  "a single battle digit passes");
    CHECK(Gate("0", DLG_MODE_BATTLE),  "zero passes -- it is the last thing a countdown says");
    CHECK(Gate("9", DLG_MODE_BATTLE),  "nine passes");
    CHECK(Gate("10", DLG_MODE_BATTLE), "two digits pass");
    // Kills dropping the mode test -- the whole point is that this is a
    // battle-only widening. A bare digit in a field window is still junk.
    CHECK(!Gate("5", MODE_FIELD),      "a single digit in the FIELD is still rejected");
    // Kills widening to "any short text in battle".
    CHECK(!Gate("K", DLG_MODE_BATTLE), "a single letter in battle is still rejected");
    CHECK(!Gate("5x", DLG_MODE_BATTLE),"digit-plus-letter is still rejected");
    CHECK(!Gate("x5", DLG_MODE_BATTLE),"letter-plus-digit is still rejected");
    CHECK(!Gate(" 5", DLG_MODE_BATTLE),"a leading space is not a digit");
}

// ---------------------------------------------------------------------------
// The all-digits predicate on its own
// ---------------------------------------------------------------------------
static void TestAllDigits()
{
    CHECK(DlgTextIsAllDigits("7"),      "'7' is all digits");
    CHECK(DlgTextIsAllDigits("123"),    "'123' is all digits");
    CHECK(!DlgTextIsAllDigits(""),      "the empty string is not all digits -- nothing to say");
    CHECK(!DlgTextIsAllDigits(nullptr), "null is not all digits");
    // '/' is '0'-1 and ':' is '9'+1 -- these kill an off-by-one on either bound.
    CHECK(!DlgTextIsAllDigits("/"),     "the character below '0' is not a digit");
    CHECK(!DlgTextIsAllDigits(":"),     "the character above '9' is not a digit");
    CHECK(!DlgTextIsAllDigits("12a"),   "a trailing letter disqualifies the whole string");
    CHECK(!DlgTextIsAllDigits("a12"),   "a leading letter disqualifies the whole string");
}

// ---------------------------------------------------------------------------
// Nothing gets past on a null or empty decode, in any mode
// ---------------------------------------------------------------------------
static void TestEmpty()
{
    CHECK(!Gate(nullptr, DLG_MODE_BATTLE), "null does not pass");
    CHECK(!Gate("", DLG_MODE_BATTLE),      "empty does not pass");
    CHECK(!Gate("", MODE_FIELD),           "empty does not pass in the field either");
    // ...and an empty decode is NOT worth a log line. This is the assertion
    // that keeps the diagnostic from firing every frame on an idle window.
    CHECK(!DlgShortDropWorthLogging("", 0, MODE_FIELD, MIN_LEN),
          "an empty decode is not a drop worth logging");
    CHECK(!DlgShortDropWorthLogging(nullptr, 0, MODE_FIELD, MIN_LEN),
          "a null decode is not a drop worth logging");
}

// ---------------------------------------------------------------------------
// The drop predicate is the exact complement of the gate, for non-empty text
// ---------------------------------------------------------------------------
static void TestDropIsComplement()
{
    static const char* const samples[] = {
        "Draw", "Mega Flare", "abc", "ab", "',", "C0", "5", "0", "10", "K", "5x", " 5", "9"
    };
    static const unsigned modes[] = { MODE_FIELD, DLG_MODE_BATTLE, 0u, 2u };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        for (size_t m = 0; m < sizeof(modes)/sizeof(modes[0]); m++) {
            const char* s = samples[i];
            size_t len = strlen(s);
            bool passed  = DlgTextPassesLengthGate(s, len, modes[m], MIN_LEN);
            bool dropped = DlgShortDropWorthLogging(s, len, modes[m], MIN_LEN);
            if (passed == dropped) {
                printf("FAIL: gate and drop agree on \"%s\" mode=%u (both %d)\n",
                       s, modes[m], (int)passed);
                g_fail++;
            }
        }
    }
    // The widening is battle-only: no other mode value may open it.
    CHECK(!Gate("5", 0u), "mode 0 does not get the digit widening");
    CHECK(!Gate("5", 2u), "mode 2 does not get the digit widening");
    CHECK(!Gate("5", 4u), "mode 4 does not get the digit widening");
}

// ---------------------------------------------------------------------------
// The log cap
// ---------------------------------------------------------------------------
static void TestLogCap()
{
    CHECK(DlgShortLogAllowed(0),                      "the first drop is logged");
    CHECK(DlgShortLogAllowed(DLG_SHORT_LOG_CAP - 1),  "the last one under the cap is logged");
    CHECK(!DlgShortLogAllowed(DLG_SHORT_LOG_CAP),     "the cap itself stops logging");
    CHECK(!DlgShortLogAllowed(DLG_SHORT_LOG_CAP + 1), "and it stays stopped");
}

int main()
{
    TestOldRuleSurvives();
    TestBattleDigits();
    TestAllDigits();
    TestEmpty();
    TestDropIsComplement();
    TestLogCap();
    printf("dialog_short_text_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
