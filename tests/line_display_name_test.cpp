// line_display_name_test.cpp -- naming a trigger line from its symbol
// (#dsrc, v0.113.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills.
#include <cstdio>
#include <cstring>

#include "line_display_name_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}
static void EQ(const char* got, const char* want, const char* what)
{
    bool ok = (got == nullptr && want == nullptr) ||
              (got != nullptr && want != nullptr && strcmp(got, want) == 0);
    if (!ok) {
        printf("FAIL: %s (got %s, want %s)\n", what,
               got ? got : "(null)", want ? want : "(null)");
        g_fail++;
    }
}

static void TestPrecedence()
{
    // The field-scoped table exists to say a symbol means different things in
    // different rooms, so it has to win. ddtower3's two terminals are exactly
    // this: both are `Tanme`-family symbols the SYM table calls "Terminal".
    EQ(LineDisplayName("Steam Room Terminal", "Terminal"), "Steam Room Terminal",
       "the field-scoped name wins over the sym name");
    // Kills swapping the two.
    EQ(LineDisplayName(nullptr, "Terminal"), "Terminal",
       "the sym name is used when there is no field-scoped row");
    EQ(LineDisplayName("Capsule", nullptr), "Capsule",
       "a field-scoped name alone is enough");
    EQ(LineDisplayName(nullptr, nullptr), nullptr,
       "no table has anything to say -> no name, and the line keeps its number");
}

static void TestEmptyIsNotAName()
{
    // An empty string is not a name. Letting one through would put a blank
    // entry in the catalog, which reads as silence and is worse than
    // "Interaction 2".
    EQ(LineDisplayName("", "Terminal"), "Terminal",
       "an empty field-scoped name falls through to the sym name");
    EQ(LineDisplayName("", ""), nullptr, "two empty names are no name");
    EQ(LineDisplayName(nullptr, ""), nullptr, "an empty sym name is no name");
}

static void TestCurated()
{
    CHECK(LineNameIsCurated("Terminal"), "a real name is curated");
    CHECK(LineNameIsCurated("Desk"),     "so is the one that already worked");
    // Kills dropping the empty check: a curated-but-blank name would survive
    // the renumbering pass and leave the entry nameless.
    CHECK(!LineNameIsCurated(""),        "an empty name is NOT curated");
    CHECK(!LineNameIsCurated(nullptr),   "no name is NOT curated");
}

int main()
{
    TestPrecedence();
    TestEmptyIsNotAName();
    TestCurated();
    printf("line_display_name_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
