// field_overlay_test.cpp -- the overlay's arithmetic, checked without a screen.
//
//   g++ -std=c++17 -O0 -Isrc -Itests -o field_overlay_test tests/field_overlay_test.cpp
//
// The rasteriser is GDI and the blit is OpenGL, and neither can run here. What
// CAN run here is every number that decides where the box goes and how big it
// is -- and that is not a small thing to check, because "the box runs off the
// bottom of the screen" is a bug this project has already shipped once
// (v0.20.125, the Garden battle's briefing) and it was pure arithmetic then too.
//
// Measure(), ZoomFor() and PlaceFor() are therefore defined free of Windows and
// of GL, and this file compiles those definitions out of the real source rather
// than re-stating them.

#include <cstdio>
#include <cstring>
#include <string>

// The three pure functions, lifted from src/field_overlay.cpp by including it
// with everything else compiled out. Simpler and safer than a copy: a copy
// would be a second implementation nothing checks against the first, which is
// exactly what tests/lint_stub.py exists to prevent.
#include "field_overlay.h"          // declarations only -- no Windows, no GL
#include "field_overlay_pure.inl"     // ...and the real definitions

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }

int main()
{
    using namespace FieldOverlay;
    std::printf("field_overlay_test\n");

    // The space rescue's own screen: ten lines, widest 32 columns.
    const char* SCREEN =
        "REACHING RINOA\n"
        "Nothing moves until you press\n"
        "Enter. Take as long as you like.\n"
        "HOLD an arrow down, don't tap.\n"
        "Two at once makes a diagonal.\n"
        "Boost: hold W.\n"
        "4x speed; burns boost fuel.\n"
        "\"Centred\" means let go.\n"
        "Only the very end counts.\n"
        "Enter start   / repeat   F9 skip";
    {
        Layout L = Measure(SCREEN, 11, 22, 10, 900, 560);
        check(L.lines == 10, "ten lines counted");
        check(L.cols == 32, "widest line is 32 columns");
        check(!L.clamped, "**and it fits** -- nothing was cut");
        check(L.w == 32 * 11 + 20 && L.h == 10 * 22 + 20,
              "the bitmap is the text plus one margin each side");
        check(L.w <= 900 && L.h <= 560, "and inside the cap");
    }

    // Text that does NOT fit is cut, and SAYS it was cut. A caller that hands
    // over too much must find that out from the return value rather than from a
    // player who cannot see the last line.
    {
        std::string wide(400, 'X');
        Layout L = Measure(wide.c_str(), 11, 22, 10, 900, 560);
        check(L.clamped, "**an over-wide line reports clamped**");
        check(L.w <= 900, "and is cut to the cap, not drawn past it");
        std::string tall;
        for (int i = 0; i < 90; i++) tall += "line\n";
        Layout T = Measure(tall.c_str(), 11, 22, 10, 900, 560);
        check(T.clamped, "**and so does an over-tall block**");
        check(T.h <= 560, "cut to the cap");
    }

    // Degenerate input is a zero box, not a crash and not a giant one.
    {
        Layout L = Measure(nullptr, 11, 22, 10, 900, 560);
        check(L.w == 0 && L.h == 0, "null text measures nothing");
        Layout Z = Measure("hi", 0, 0, 10, 900, 560);
        check(Z.w == 0 && Z.h == 0, "a zero cell measures nothing");
    }

    // ZOOM. Integer only, so the text stays crisp; and never so much that the
    // box stops fitting -- which is the v0.20.125 bug in its new clothes.
    {
        check(ZoomFor(480, 240) == 1, "480p draws at 1x");
        check(ZoomFor(960, 240) == 2, "960p draws at 2x");
        check(ZoomFor(1440, 240) == 3, "1440p draws at 3x");
        check(ZoomFor(4320, 240) == 4, "and it caps at 4x");
        check(ZoomFor(800, 240) == 1, "800p (Aaron's window) draws at 1x");
        // A tall box on a short viewport must come back DOWN, not overflow.
        check(ZoomFor(960, 500) * 500 <= 960,
              "**a big box on a small screen zooms down until it fits**");
        check(ZoomFor(960, 500) >= 1, "but never below 1x");
        check(ZoomFor(0, 240) == 1 && ZoomFor(960, 0) == 1,
              "a viewport or bitmap of nothing still returns a usable zoom");
    }

    // PLACEMENT. Centred, and never negative -- glRasterPos2i off-screen is
    // silently dropped by GL, which looks exactly like the overlay not working.
    {
        int x = -1, y = -1;
        PlaceFor(1280, 800, 372, 240, 1, &x, &y);
        check(x == (1280 - 372) / 2 && y == (800 - 240) / 2, "centred at 1x");
        PlaceFor(1280, 800, 372, 240, 2, &x, &y);
        check(x == (1280 - 744) / 2 && y == (800 - 480) / 2,
              "**and the zoom is in the centring** -- the box grows from the "
              "middle, not from the corner");
        PlaceFor(320, 240, 900, 560, 1, &x, &y);
        check(x == 0 && y == 0,
              "**a box bigger than the screen starts at the origin** -- a "
              "negative raster position is dropped by GL and reads as 'the "
              "overlay does not work'");
    }

    std::printf(bad ? "FAIL: %d\n" : "OK (%d failures)\n", bad);
    return bad ? 1 : 0;
}
