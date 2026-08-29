// field_overlay.h -- the mod's own on-screen text, drawn independently of the
// field.
//
// WHY THIS EXISTS
// ---------------
// Aaron, after the v0.64.x field freeze put a real pause behind the space
// rescue's Game Controls screen: *"Now that is working is there anything we can
// do to get the Game Controls visible on the screen? Can we independently fire
// the dialog renderer or something like that while freezing the scene?"*
//
// The game's dialog renderer cannot be fired on its own. field_main's steady
// state (0x0047246E) is one instruction -- `call 0x004767B0` -- and that single
// function is the whole per-frame field: scripts, movement and window drawing
// together, with no "draw only" entry to call. Freezing the field therefore
// takes the box with it, which is what his F11 screenshot showed: a black frame
// with nothing on it but the mission timer HUD, which survives because it is
// drawn AFTER field_main in field_main_loop.
//
// So the mod draws its own box from the SwapBuffers hook, on the game's own
// render thread with a live OpenGL context.
//
// v0.66.2: IT WORKS, and here is the evidence, because five builds of this file
// have carried confident claims that turned out to be wrong. The 13:12 capture
// (Logs/screenshots/overlay_*.png, 640x480) shows the box at (134,120), 372x240,
// ten lines of white text on the dark ground with its light border, beside the
// game's own 1:28 timer HUD on an otherwise black frame. The measurement agrees
// with the picture: "IN THE CAPTURE: ink per line 97,157,136,141,128,68,137,102,
// 124,137" -- the same ten numbers the read-back reports, taken from the bytes
// that became that PNG.
//
// WHAT WAS ACTUALLY WRONG, after four wrong answers: bgfx leaves a SHADER
// PROGRAM bound. Every fixed-function fragment the overlay produced went through
// it, was resolved against bgfx's own textures, and vanished -- with glGetError
// clean the whole time, which is why nothing ever looked broken. Unbinding it is
// the fix. The diagnostic that proved it also ruled the alternatives out: the
// beacon drawn with a bare glDrawPixels came back at exactly 0/255/255, so the
// pixel path was never the problem and neither was the RGBA stride.
//
// v0.66.1 CORRECTION, and it is the correction three builds needed. The line
// that used to sit here said the SwapBuffers hook is "the one part of the
// render path the field freeze does not touch", and that the F11 screenshot
// taken during a pause proved it. Both halves are wrong. WHILE THE FIELD IS
// FROZEN THE GAME DOES NOT PRESENT: SwapBuffers is not called, so this hook
// does not run, so nothing draws and nothing captures. The 12:39 BAT is
// unambiguous -- two F11 presses during the pause produced one file, written at
// the exact second Enter released the freeze.
//
// That is not fatal, and may even be a gift: if the engine stops presenting,
// THE LAST FRAME IT PRESENTED STAYS ON THE SCREEN. So the box does not need to
// be drawn during the pause at all -- it needs to be drawn into the frame that
// is on screen WHEN the pause begins, which is exactly what the SP_DRAW_MS
// delay between opening the screen and engaging the freeze already arranges.
// What could never work was CAPTURING it: a capture needs a swap, the next swap
// is the one after Enter, and by then the box has been hidden.
//
// So the box is ours now. That is better than restoring the game's: it is not
// bound to the game's 320x224 window geometry or its 34-column wrap, and every
// Game Controls screen in the mod can use it, not just this one.
//
// HOW IT IS SPLIT
// ---------------
// Show() runs on the MOD thread and rasterises the text into an RGB buffer with
// GDI. Draw() runs on the RENDER thread inside the SwapBuffers hook and does
// nothing but blit that buffer. No allocation, no GDI and no logging happen on
// the render thread; the buffer is built complete and then published, and it is
// never freed while the overlay is shown.

#pragma once

namespace FieldOverlay {

    // Rasterise `text` (lines separated by '\n') and start drawing it every
    // frame. Replaces whatever was shown. Returns false if the text could not
    // be rasterised, in which case nothing is shown and nothing changes.
    bool Show(const char* text);

    // Stop drawing. Idempotent.
    void Hide();

    bool IsShown();

    // Called from the SwapBuffers hook, on the game's render thread, with the
    // GL context current. No-op unless something is shown.
    void Draw();

    // v0.65.6: THE CAPTURE'S OWN OPINION OF WHAT IT JUST READ.
    //
    // Draw() reads the frame buffer back immediately after its own blit and
    // reports ten lines of ink. The F11 capture, reading the same buffer from
    // the same hook a few instructions later, contains none of it -- not one
    // pixel of the overlay's background (12,16,32) or its border (200,210,235)
    // anywhere in the 1280x800 frame. Two glReadPixels calls cannot disagree
    // about one buffer, so one of them is not reading what it thinks it is.
    // This is called with the pixels DoGLCapture actually wrote to disk, so the
    // measurement and the file are the same data and cannot drift apart.
    //
    // `bgr` is bottom-up GL_BGR_EXT, `stride` bytes per row. Logs once per
    // Show(). No-op unless something is shown.
    void ProbeCapture(const unsigned char* bgr, int w, int h, int stride);

    // v0.66.1 (#111): COUNT THE SWAPS.
    //
    // field_overlay.h has said since v0.65.0 that the SwapBuffers hook is "the
    // one part of the render path the field freeze does not touch". That is
    // FALSE, and the 12:39 BAT proves it: while the field is frozen the game
    // does not present at all. Two F11 presses during the pause produced ONE
    // file, written at 12:39:51.996 -- the exact second Enter released the
    // freeze -- because neither request could be serviced until a swap
    // happened. The overlay's own "first blit" line appears once, in the 200 ms
    // between Show() and the freeze engaging, and never again.
    //
    // NoteSwap() is called unconditionally from the top of the hook so the
    // count is a fact about the ENGINE, not about whether the overlay happened
    // to be drawing. FieldPause's callers record it at engage and read it at
    // release; a delta of zero across a pause is the whole claim, measured.
    void     NoteSwap();
    unsigned SwapCount();

    // Frees the rasterised buffer. Called once at shutdown.
    void Shutdown();

    // --- pure, and therefore testable off-Windows ---------------------------
    struct Layout {
        int lines;      // how many lines the text has
        int cols;       // the widest line, in characters
        int w, h;       // the bitmap the rasteriser will produce, in pixels
        bool clamped;   // true if the text had to be cut to fit the cap
    };
    // Measures what Show() will produce. cellW/cellH are the fixed-pitch cell
    // size; pad is the margin on each side. Never returns a size above the cap.
    Layout Measure(const char* text, int cellW, int cellH, int pad,
                   int maxW, int maxH);

    // The zoom Draw() will use for a viewport of this height, and where it will
    // put the bitmap. Separated out for the same reason: it is arithmetic, and
    // arithmetic can be checked without a screen.
    int  ZoomFor(int viewportH, int bitmapH);
    void PlaceFor(int viewportW, int viewportH, int bitmapW, int bitmapH,
                  int zoom, int* outX, int* outY);
}
