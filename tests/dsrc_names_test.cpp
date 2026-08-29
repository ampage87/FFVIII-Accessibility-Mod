// dsrc_names_test.cpp -- the Deep Sea Research Center's floor numbers
// (#dsrc, v0.112.0).
//
// Aaron: "Floor numbers between the catalog and the game seemed off... I think
// they were off by one." They were, for the whole block. These assertions are
// the game's own terminal text, quoted from each field's .msd, so the numbering
// cannot drift back without a test failing.
#include <cstdio>
#include <cstring>

#include "field_display_names.h"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

static void Expect(int id, const char* internalName, const char* display, const char* why)
{
    if (id < 0 || id >= (int)(sizeof(FIELD_DISPLAY_NAMES)/sizeof(FIELD_DISPLAY_NAMES[0]))) {
        printf("FAIL: id %d out of range\n", id); g_fail++; return;
    }
    if (strcmp(FIELD_INTERNAL_NAMES[id], internalName) != 0) {
        printf("FAIL: id %d is '%s', expected internal '%s'\n",
               id, FIELD_INTERNAL_NAMES[id], internalName);
        g_fail++; return;
    }
    if (strcmp(FIELD_DISPLAY_NAMES[id], display) != 0) {
        printf("FAIL: %s (id %d) reads \"%s\", expected \"%s\"  [%s]\n",
               internalName, id, FIELD_DISPLAY_NAMES[id], display, why);
        g_fail++;
    }
}

int main()
{
    // The two arrays must stay the same length, or every id past the gap is
    // announcing the wrong room. This is the shape of the bug that produced the
    // off-by-one in the first place.
    CHECK(sizeof(FIELD_DISPLAY_NAMES) == sizeof(FIELD_INTERNAL_NAMES),
          "the display and internal name tables are the same length");

    // ddtowerN is Level N. Each line quotes the terminal that says so.
    Expect(301, "ddtower1", "Deep Sea Research Center - Level 1",
           "msg21: \"Reset confirmed.  Door to level 2 unlocked.\"");
    Expect(302, "ddtower2", "Deep Sea Research Center - Level 2",
           "msg8: \"Opening door to level 3.\"");
    Expect(303, "ddtower3", "Deep Sea Research Center - Level 3",
           "msg26: \"Opening door to level 4.\"");
    Expect(304, "ddtower4", "Deep Sea Research Center - Level 4",
           "msg0: \"Opening door to level 5.\"");
    Expect(305, "ddtower5", "Deep Sea Research Center - Level 5",
           "msg0: \"Opening door to level 6.\"");
    Expect(306, "ddtower6", "Deep Sea Research Center - Level 6",
           "msg0: \"Opening door to excavation site\" -- the bottom of a 6 level tower");

    // And the steam room is not a level. It is what pushed every floor up by
    // one, because the source numbered it as one.
    Expect(300, "ddsteam1", "Deep Sea Research Center - Steam Room",
           "ddtower3 msg27: \"Opening door to the Steam Room.\"");

    // The ruins below keep their own run, which was never shifted.
    Expect(294, "ddruins1", "Deep Sea Deposit 1", "the excavation site, unchanged");
    Expect(299, "ddruins6", "Deep Sea Deposit 6", "the excavation site, unchanged");

    // The neighbours on either side of the block, so a future edit that shifts
    // the array shows up here rather than in a corridor.
    Expect(293, "cwwood7", "Chocobo Forest 7", "the entry before the block");
    Expect(307, "doan1_1", "Dollet - Comm Tower 1", "the entry after the block");

    printf("dsrc_names_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
