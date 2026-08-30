// line_event_surface_test.cpp -- event lines that wait on a button (#shumi,
// v0.132.2). Masks are read out of the field archive: Cross|Square is 0x00C0,
// Square alone is 0x0040.
#include <cstdio>

#include "line_event_surface_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // The wind stone. Both halves of it, in Village 1, typed LINE_EVENT because
    // they REQ a method on the party leader instead of carrying dialogue.
    CHECK(EventLineSurfaces(true, 0x00C0), "tmgate1's Mitukeru waits on Cross|Square and is an interaction");
    CHECK(EventLineSurfaces(true, 0x00C0), "and so is Mitukeru2");
    // tmmura2's Mitukeru, and the Artisan's-house spot on Square alone.
    CHECK(EventLineSurfaces(true, 0x0040), "tmmin1's Hakken waits on Square");

    // THE 232 THAT STAY SCENERY. A LINE_EVENT with no button is a SHOW/HIDE or
    // BATTLE line -- a visual effect the player never chooses -- and putting one
    // in the catalog offers a blind player something that cannot be used.
    CHECK(!EventLineSurfaces(true, 0x0000), "an event line that waits on nothing stays scenery");

    // OTHER LINE TYPES ARE NOT THIS RULE'S BUSINESS. They reach the catalog
    // through paths that already exist; answering true would emit them twice.
    CHECK(!EventLineSurfaces(false, 0x00C0), "an interactive line is already handled elsewhere");
    CHECK(!EventLineSurfaces(false, 0x0000), "and so is everything else");

    if (g_fail == 0) printf("line_event_surface_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
