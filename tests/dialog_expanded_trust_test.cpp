// dialog_expanded_trust_test.cpp -- the garble filter and built messages
// (#centra, v0.117.0).
//
// Each assertion was written against a mutant. The rejected string is the real
// one out of the 2026-08-28 log.
#include <cstdio>
#include <cstring>
#include <string>

#include "dialog_expanded_trust_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

// The letter-ratio rule that killed the code, reproduced exactly as
// IsGarbledText applies it, so the numbers below are the real ones and not an
// impression of them.
static int LetterPct(const std::string& t)
{
    int letters = 0;
    for (size_t i = 0; i < t.size(); i++) {
        const unsigned char c = (unsigned char)t[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters++;
    }
    return t.empty() ? 0 : (letters * 100) / (int)t.size();
}

int main()
{
    // THE CASE. Both the old rendering and the corrected one are under the
    // filter's 30% letter floor, so fixing 0x3A alone would NOT have made the
    // code speak. That is the whole reason the exemption exists.
    CHECK(LetterPct("Code:9'8'9'3'9") < 30, "the old rendering trips letterPct");
    CHECK(LetterPct("Code:9 8 9 3 9") < 30, "the corrected rendering trips it too");
    // ...and it is not a length artefact: the filter only judges len >= 8, and
    // both of these are well past that.
    CHECK(std::string("Code:9 8 9 3 9").size() >= 8, "long enough to be judged");

    // THE RULE. One resolved insert is enough: to report even one the expander
    // had to find a well-formed 0x04+param whose param the engine's own
    // resolver accepts.
    CHECK(DetExpansionIsTrusted(1), "one insert is evidence of a real message");
    CHECK(DetExpansionIsTrusted(5), "five inserts, as in the Centra code");
    CHECK(!DetExpansionIsTrusted(0), "no inserts, no exemption");
    // Kills `insertCount >= 0`, which would exempt every message and retire the
    // Fire Cavern filter entirely.
    CHECK(!DetShouldRunGarbleFilter(1), "an expanded message skips the filter");
    CHECK(DetShouldRunGarbleFilter(0), "an ordinary message still faces it");
    // A negative count cannot mean "trusted" -- nothing produces one today, but
    // a future caller that forgets to initialise its local must fail closed.
    CHECK(!DetExpansionIsTrusted(-1), "a negative count is not trust");
    CHECK(DetShouldRunGarbleFilter(-1), "a negative count still filters");

    // The two call sites must agree; they share this one predicate so that a
    // change to the policy cannot land in one path and miss the other.
    for (int n = -2; n <= 3; n++) {
        CHECK(DetShouldRunGarbleFilter(n) == !DetExpansionIsTrusted(n),
              "the two halves of the policy are one statement");
    }

    printf("dialog_expanded_trust_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
