// field_overlay_pure.inl -- the overlay's arithmetic, with no Windows and no GL
// in it, so tests/field_overlay_test.cpp can compile THESE DEFINITIONS rather
// than a second copy of them. Included by src/field_overlay.cpp for the real
// build and by the probe for the test; the two never link together, so there is
// no ODR question to answer.
//
// It is not a small thing to check. "The box runs off the bottom of the screen"
// is a bug this project has already shipped once -- v0.20.125, the Garden
// battle's briefing -- and it was pure arithmetic then too.

namespace FieldOverlay {

static const int FP_MAX_W = 900;    // the rasterised bitmap's ceiling, in pixels
static const int FP_MAX_H = 560;

Layout Measure(const char* text, int cellW, int cellH, int pad,
               int maxW, int maxH)
{
    Layout L = {};
    if (!text || cellW <= 0 || cellH <= 0) return L;

    int cols = 0, col = 0, lines = 1;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { if (col > cols) cols = col; col = 0; lines++; }
        else col++;
    }
    if (col > cols) cols = col;

    // Clamp to the cap in BOTH directions, and say so: a caller that hands over
    // more than fits should find out from the return value rather than from a
    // player who cannot see the last line.
    const int fitCols  = (maxW - pad * 2) / cellW;
    const int fitLines = (maxH - pad * 2) / cellH;
    if (fitCols > 0 && cols > fitCols)   { cols = fitCols;   L.clamped = true; }
    if (fitLines > 0 && lines > fitLines){ lines = fitLines; L.clamped = true; }
    if (cols < 1) cols = 1;
    if (lines < 1) lines = 1;

    L.cols  = cols;
    L.lines = lines;
    L.w     = cols  * cellW + pad * 2;
    L.h     = lines * cellH + pad * 2;
    return L;
}

// Integer zoom, so the text stays crisp rather than resampled. One at 480p and
// below, two at 960, three at 1440 -- the game runs windowed at all sorts of
// sizes and a fixed 22-pixel font is unreadable on a 4K panel.
int ZoomFor(int viewportH, int bitmapH)
{
    if (viewportH <= 0 || bitmapH <= 0) return 1;
    int z = viewportH / 480;
    if (z < 1) z = 1;
    if (z > 4) z = 4;
    // ...but never so much that the box no longer fits on the screen.
    while (z > 1 && bitmapH * z > viewportH) z--;
    return z;
}

// glRasterPos2i takes the BOTTOM-left corner, because GL's origin is bottom
// left. Getting that backwards puts the box off the top of the screen, which
// looks exactly like the overlay not working at all.
void PlaceFor(int viewportW, int viewportH, int bitmapW, int bitmapH,
              int zoom, int* outX, int* outY)
{
    if (zoom < 1) zoom = 1;
    const int w = bitmapW * zoom, h = bitmapH * zoom;
    int x = (viewportW - w) / 2;
    int y = (viewportH - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (outX) *outX = x;
    if (outY) *outY = y;
}

} // namespace FieldOverlay
