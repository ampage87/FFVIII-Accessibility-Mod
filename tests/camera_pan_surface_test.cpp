// camera_pan_surface_test.cpp -- surfacing a NAMED camera-pan line (#centra, v0.115.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills. The rule is deliberately narrow: the NAME is the gate, not
// the opcode, because 95 camera-pan lines disc-wide share the PREQEW dispatch
// and most of them are auto-firing story triggers.
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdio>

#include "line_display_name_model.inl"
#include "line_camera_pan_surface_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // A named camera pan surfaces. This is the whole point: crtower3's `ladup0`
    // is the only way up out of the Centra Ruins tower and was invisible.
    CHECK(CameraPanLineSurfaces(true, "Ladder Up"),
          "a named camera-pan line surfaces");

    // An UNNAMED camera pan does not. Kills `return isCameraPan;` -- that
    // mutant would surface all 95 PREQEW lines plus every genuine camera pan
    // in the game, including fhtown22's `blocker1` and gmcont1's `CantGoNext`.
    CHECK(!CameraPanLineSurfaces(true, nullptr),
          "an unnamed camera-pan line stays hidden");

    // An EMPTY name is not a name. Kills `curatedName != nullptr` alone, which
    // would surface a line as "" -- an entry a player can select and cannot
    // identify, which is worse than not offering it.
    CHECK(!CameraPanLineSurfaces(true, ""),
          "an empty name does not surface a camera-pan line");

    // A non-camera-pan line is never this function's business, named or not.
    // Kills `return LineNameIsCurated(curatedName);` with the isCameraPan test
    // dropped: every named INTERACTIVE line already reaches the catalog through
    // the path that existed, and answering true here would emit it twice.
    CHECK(!CameraPanLineSurfaces(false, "Ladder Up"),
          "a named non-camera-pan line is not surfaced by this rule");
    CHECK(!CameraPanLineSurfaces(false, nullptr),
          "an unnamed non-camera-pan line is not surfaced by this rule");

    // The predicate agrees with the one v0.113.0 already uses to decide whether
    // a name outranks "Interaction N". If these two ever disagree a line could
    // surface and then be renumbered back to a number, which is the exact bug
    // v0.113.0 fixed. Pinned so a change to either is a change to both.
    const char* cases[] = { "Ladder Up", "Eye Statue", "", nullptr };
    for (int i = 0; i < 4; i++) {
        CHECK(CameraPanLineSurfaces(true, cases[i]) == LineNameIsCurated(cases[i]),
              "surfacing agrees with LineNameIsCurated");
    }


    // ------------------------------------------------------------------
    // v0.116.0: a NAMED line the engine has switched off.
    // ------------------------------------------------------------------
    // crtower1's `console0` is LINEOFF at every load until the power is on,
    // and again the moment it has been used. It still exists, and a player who
    // hears nothing cannot tell that from a broken mod.
    CHECK(CameraPanLineSurfacesOffToo(true, "Control Panel"),
          "a named camera pan surfaces whether the engine has it on or off");
    CHECK(!CameraPanLineSurfacesOffToo(true, nullptr),
          "an unnamed camera pan stays hidden when switched off");

    // The suffix. Kills `return !engineActive;` -- that mutant would produce a
    // bare ", not active" entry for an unnamed line.
    CHECK(LineOffSuffixApplies("Control Panel", false), "off + named -> suffix");
    CHECK(!LineOffSuffixApplies("Control Panel", true), "on -> no suffix");
    CHECK(!LineOffSuffixApplies(nullptr, false), "off + unnamed -> no suffix");
    CHECK(!LineOffSuffixApplies("", false), "off + empty name -> no suffix");

    {
        char buf[64];
        LineOffDisplayName(buf, sizeof(buf), "Control Panel", false);
        CHECK(strcmp(buf, "Control Panel, not active") == 0, "off name carries the suffix");
        LineOffDisplayName(buf, sizeof(buf), "Control Panel", true);
        CHECK(strcmp(buf, "Control Panel") == 0, "a live line keeps its plain name");
        LineOffDisplayName(buf, sizeof(buf), nullptr, false);
        CHECK(buf[0] == '\0', "no name in, nothing out");

        // TRUNCATION KEEPS THE NAME, NOT HALF THE SUFFIX. A buffer that fits
        // "Control Panel" but not the suffix must yield "Control Panel" --
        // "Control Panel, not a" is an entry a player cannot act on.
        char tight[16];
        LineOffDisplayName(tight, sizeof(tight), "Control Panel", false);
        CHECK(strcmp(tight, "Control Panel") == 0, "no room for the suffix -> bare name");
        char tiny[6];
        LineOffDisplayName(tiny, sizeof(tiny), "Control Panel", false);
        CHECK(strlen(tiny) < sizeof(tiny), "a tiny buffer stays terminated");
    }

    printf("camera_pan_surface_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
