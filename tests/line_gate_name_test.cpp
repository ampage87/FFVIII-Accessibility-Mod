// line_gate_name_test.cpp -- saying that a terminal is not ready
// (#dsrc, v0.114.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills.
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "line_gate_name_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}
static void EQ(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL: %s (got \"%s\", want \"%s\")\n", what, got, want);
        g_fail++;
    }
}

static void TestName()
{
    char buf[64];
    LineGateName(buf, sizeof(buf), "Steam Room Terminal", false);
    EQ(buf, "Steam Room Terminal, not ready", "a shut terminal says so, name first");
    // Kills suffixing unconditionally -- which would put ", not ready" on every
    // working terminal on the disc.
    LineGateName(buf, sizeof(buf), "Steam Room Terminal", true);
    EQ(buf, "Steam Room Terminal", "an open terminal reads as its plain name");
    LineGateName(buf, sizeof(buf), "Level Terminal", false);
    EQ(buf, "Level Terminal, not ready", "and the other one on that floor too");
}

static void TestDegenerate()
{
    char buf[64];
    // An empty base must not produce a bare ", not ready" -- which reads as a
    // nameless entry and is worse than no entry.
    LineGateName(buf, sizeof(buf), "", false);
    EQ(buf, "", "an empty name stays empty rather than becoming the suffix alone");
    LineGateName(buf, sizeof(buf), nullptr, false);
    EQ(buf, "", "a null name stays empty");
    char guard[4] = { 'x', 'x', 'x', 'x' };
    LineGateName(guard, 0, "Terminal", false);
    CHECK(guard[0] == 'x', "a zero-length buffer is left alone");
    LineGateName(nullptr, 16, "Terminal", false);   // must not crash
    // The suffix must survive truncation without corrupting the buffer.
    char tiny[8];
    LineGateName(tiny, sizeof(tiny), "Steam Room Terminal", false);
    CHECK(tiny[sizeof(tiny) - 1] == '\0', "a short buffer is still terminated");
}

static void TestWhoGetsTheSuffix()
{
    // The Level 3 case: a named line whose script is shut.
    CHECK(LineGateSuffixApplies(true, true, false), "a named, gated, shut line says so");
    // Kills dropping any one of the three conditions.
    CHECK(!LineGateSuffixApplies(true, true, true),  "a named line that is OPEN does not");
    CHECK(!LineGateSuffixApplies(true, false, false),"a line with no decodable gate does not");
    // "Interaction 2, not ready" tells the player nothing they can act on, and
    // there are hundreds of anonymous lines whose guards this has never seen.
    CHECK(!LineGateSuffixApplies(false, true, false), "an UNNAMED line never gets the suffix");
    CHECK(!LineGateSuffixApplies(false, false, true), "and neither does anything else");
}

int main()
{
    TestName();
    TestDegenerate();
    TestWhoGetsTheSuffix();
    printf("line_gate_name_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
