// field_overlay.cpp -- see field_overlay.h for why this exists.

#include "field_overlay.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <gl/GL.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif

// ---------------------------------------------------------------------------
// WHAT WE ARE DRAWING INTO, AND WHY IT MATTERS (v0.65.7, #111)
// ---------------------------------------------------------------------------
// FFNx's own log names the renderer: "Backend: OpenGL", through bgfx, on
// opengl32.dll. bgfx is a MODERN renderer wearing an old API's clothes, and two
// pieces of state it leaves behind will silently swallow anything a legacy
// overlay draws:
//
//   1. A FRAMEBUFFER OBJECT may be bound. Everything we draw then lands in
//      bgfx's offscreen target instead of the window -- and, damningly, a
//      glReadPixels taken straight afterwards READS THAT SAME TARGET and finds
//      our pixels exactly where we put them. That is the contradiction the last
//      three builds could not explain: Draw()'s read-back saw ten lines while
//      the F11 capture, a different frame later, saw none.
//
//   2. A SHADER PROGRAM may be bound. With a program current, the fragments
//      glDrawPixels and glBegin produce go through bgfx's fragment shader,
//      which samples ITS textures and knows nothing about our colours. The draw
//      "succeeds", glGetError stays clean, and nothing recognisable appears.
//
// So before drawing anything: unbind the framebuffer, unbind the program, and
// put the active texture unit back to 0. All three are saved and restored, and
// all three are logged once, because if any of them was non-zero that fact
// alone explains three builds of confusion.
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

// The arithmetic lives in its own file so the probe can compile the same
// definitions this build uses.
#include "field_overlay_pure.inl"

namespace FieldOverlay {

// ---------------------------------------------------------------------------
// The rasteriser. Mod thread only.
// ---------------------------------------------------------------------------

static const int FP_CELL_H = 22;
static const int FP_PAD    = 10;

static uint8_t* s_pixels   = nullptr;   // RGB, bottom-up, published complete
static int      s_w        = 0;
static int      s_h        = 0;
static volatile bool s_show = false;
static int      s_cellW    = 0;
static int      s_cellH    = 0;   // published for the read-back's row maths
static int      s_padPx    = 0;
static bool     s_drawLogged = false;

// v0.65.6: where the last blit went, so the capture probe can look in the same
// place, and a latch so the probe reports once per Show() rather than per F11.
static volatile int  s_lastX = 0;
static volatile int  s_lastY = 0;
static volatile int  s_lastZoom = 1;
static volatile bool s_probed = false;

// THE TWO BEACONS. Both are drawn only while the overlay is shown, both in the
// bottom-right corner where nothing else is, and they exist to split one
// question into two answerable ones:
//
//   A -- a solid quad through glBegin/glEnd. Fixed-function GEOMETRY.
//   B -- a solid block through glDrawPixels. The PIXEL-TRANSFER path, with
//        forty-eight rows of one constant colour, so nothing about strides,
//        formats or row alignment can matter.
//
// The overlay itself is B with complicated data. If B shows up and the overlay
// does not, the fault is in our buffer. If A shows up and B does not,
// glDrawPixels is the wrong call to be making against this renderer and the
// box becomes a textured quad. If neither shows up, nothing this hook draws is
// ever presented and the whole approach needs replacing -- which is worth
// knowing after three builds of assuming otherwise.
// The three pieces of bgfx state we have to step around, resolved once through
// wglGetProcAddress. Any of them may legitimately be null on a driver that does
// not have the entry point; every use is guarded and the overlay still draws
// (badly, as it has been) rather than crashing.
typedef void (APIENTRY *FP_BindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *FP_UseProgram)(GLuint);
typedef void (APIENTRY *FP_ActiveTexture)(GLenum);
typedef void (APIENTRY *FP_BindVertexArray)(GLuint);
static FP_BindFramebuffer s_glBindFramebuffer = nullptr;
static FP_UseProgram      s_glUseProgram      = nullptr;
static FP_ActiveTexture   s_glActiveTexture   = nullptr;
static FP_BindVertexArray s_glBindVertexArray = nullptr;
static bool               s_glResolved        = false;

static void ResolveGL()
{
    if (s_glResolved) return;
    s_glResolved = true;
    s_glBindFramebuffer = (FP_BindFramebuffer)wglGetProcAddress("glBindFramebuffer");
    if (!s_glBindFramebuffer)
        s_glBindFramebuffer = (FP_BindFramebuffer)wglGetProcAddress("glBindFramebufferEXT");
    s_glUseProgram      = (FP_UseProgram)wglGetProcAddress("glUseProgram");
    s_glActiveTexture   = (FP_ActiveTexture)wglGetProcAddress("glActiveTexture");
    if (!s_glActiveTexture)
        s_glActiveTexture = (FP_ActiveTexture)wglGetProcAddress("glActiveTextureARB");
    s_glBindVertexArray = (FP_BindVertexArray)wglGetProcAddress("glBindVertexArray");
}

// The texture the box is uploaded into. A textured quad is how every overlay
// that has to survive an unknown renderer draws itself -- glDrawPixels depends
// on a valid raster position, obeys glPixelZoom, and is the first thing a
// translation layer implements badly. Both paths are drawn: identical pixels in
// the same place, so whichever one the renderer honours, the box appears once.
// Power-of-two dimensions, because a GL 1.1 promise is worth more here than the
// memory an NPOT texture would save.
// Swaps the engine has actually presented. See the header.
static volatile unsigned s_swaps = 0;
void     NoteSwap()  { s_swaps++; }
unsigned SwapCount() { return s_swaps; }

static GLuint s_tex     = 0;
static int    s_texW    = 0;
static int    s_texH    = 0;
static bool   s_texFresh = false;   // the bitmap changed; re-upload on next Draw

static int PotAtLeast(int v)
{
    int p = 1;
    while (p < v && p < 4096) p <<= 1;
    return p;
}

// THE BEACONS ARE DONE. They answered the question they were built for, in one
// run: the 13:12 capture has beacon A at exactly 255/0/255 and beacon B at
// exactly 0/255/255, which says fixed-function GEOMETRY and fixed-function
// PIXEL TRANSFER both reach the screen once bgfx's shader program is unbound.
// That also identifies which of v0.66.0's two changes was the fix: beacon B is
// a plain glDrawPixels, so the pixel path was never broken -- the bound program
// was eating its fragments. Gated off rather than deleted, per the house
// pattern: a diagnostic that has to be rewritten from scratch is one nobody
// reruns. Set to 1 and they come back.
#define FP_BEACONS 0
static const int  FP_BEACON = 48;
static const int  FP_BEACON_MARGIN = 16;
static uint8_t    s_beaconB[FP_BEACON * FP_BEACON * 4];
static bool       s_beaconBReady = false;

bool IsShown() { return s_show; }

void Hide()
{
    if (!s_show) return;
    s_show = false;
    Log::Field("FieldNavigation: [OVERLAY] hidden");
}

void Shutdown()
{
    s_show = false;
    if (s_pixels) { free(s_pixels); s_pixels = nullptr; }
    s_w = s_h = 0;
    // s_tex is deliberately NOT deleted. Shutdown runs on the mod thread, which
    // has no GL context; glDeleteTextures there is undefined at best. One
    // texture object outliving the process by a few milliseconds is the correct
    // trade against calling GL from the wrong thread.
}

bool Show(const char* text)
{
    if (!text || !text[0]) return false;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) return false;

    // A fixed-pitch face, so Measure()'s cell arithmetic is the truth rather
    // than an estimate. Consolas ships with every Windows this game runs on;
    // Courier New is the fallback, and FIXED_PITCH|FF_MODERN makes the mapper
    // pick another monospace face rather than a proportional one if neither is
    // present.
    HFONT font = CreateFontA(FP_CELL_H, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!font)
        font = CreateFontA(FP_CELL_H, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    if (!font) { DeleteDC(dc); return false; }
    HGDIOBJ oldFont = SelectObject(dc, font);

    TEXTMETRICA tm = {};
    GetTextMetricsA(dc, &tm);
    const int cellW = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : (FP_CELL_H / 2);
    const int cellH = tm.tmHeight > 0 ? tm.tmHeight : FP_CELL_H;

    const Layout L = Measure(text, cellW, cellH, FP_PAD, FP_MAX_W, FP_MAX_H);
    if (L.w <= 0 || L.h <= 0) {
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc); return false;
    }

    // A bottom-up 32-bit DIB, because that is the row order glDrawPixels wants
    // and converting it twice would be work for nothing.
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = L.w;
    bi.bmiHeader.biHeight = L.h;          // positive = bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc); return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, dib);

    // Background, then a one-pixel border, then the text. Dark box, light text:
    // the same contrast the game's own window uses, and the one a sighted person
    // reading over his shoulder will expect.
    RECT all = { 0, 0, L.w, L.h };
    HBRUSH bg = CreateSolidBrush(RGB(12, 16, 32));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(RGB(200, 210, 235));
    FrameRect(dc, &all, border);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 244, 255));

    int y = FP_PAD;
    const char* p = text;
    for (int line = 0; line < L.lines; line++) {
        const char* e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len > L.cols) len = L.cols;
        TextOutA(dc, FP_PAD, y, p, len);
        y += cellH;
        if (!e) break;
        p = e + 1;
    }

    // FLUSH BEFORE READING THE BITS. GDI batches: the calls above are queued,
    // not necessarily executed, and reading a DIB section's memory directly
    // does not force them out. Aaron's 2026-08-23 22:05 screenshot is what that
    // looks like -- the border and the FIRST line of ten had landed, the other
    // nine had not, and the background was still whatever the allocation
    // happened to contain. GdiFlush is the one line that makes a DIB section
    // safe to read by pointer.
    // ------------------------------------------------------------------
    // LET GO OF THE BITMAP BEFORE READING ITS MEMORY.
    // ------------------------------------------------------------------
    // v0.65.1 added GdiFlush() here and it was not enough: Aaron's 22:45
    // screenshot still showed one line of ten on a background that was never
    // painted. The rule GDI actually enforces is stronger than "flush" -- while
    // a DIB section is SELECTED INTO A DC, reading its bits by pointer is not
    // defined, and the documented way to make it defined is to deselect it
    // first. So the DC is now fully torn down before a single byte is read.
    // The DIB section's memory stays valid until DeleteObject(dib), which is
    // why that one call is left until after the copy.
    GdiFlush();
    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);

    // FOUR BYTES A PIXEL, NOT THREE.
    //
    // v0.65.2 packed to 24-bit and declared GL_RGB, and the 2026-08-23 23:39
    // BAT showed what this renderer does with that. The bitmap was PERFECT --
    // "[OVERLAY] ink per line: 1202,1823,1762,1690,1706,841,1564,1330,1420,1584",
    // all ten lines present -- and the screen still showed one line on a wash of
    // grey. The give-away is the geometry: a box declared 372 wide came out
    // about 279 on screen, and 279 is 372 * 3/4. The layer under this
    // glDrawPixels consumed our 372x3 bytes as 279 FOUR-BYTE pixels: it ignores
    // GL_RGB and the UNPACK_ALIGNMENT and reads RGBA regardless. Row 0 is
    // therefore nearly right, and every row after it drifts by a quarter of a
    // row, which is exactly the diagonal-ish banding in his screenshot and
    // exactly why only the top line survived.
    //
    // So give it what it is going to read anyway. 32-bit rows are 4-aligned by
    // construction, GL_RGBA is core GL 1.1 and needs no extension, and the DIB
    // is already 32-bit -- this copy now only swaps R and B rather than
    // repacking. There is no longer a stride for anything to disagree about.
    const size_t need = (size_t)L.w * (size_t)L.h * 4;
    uint8_t* buf = (uint8_t*)malloc(need);
    if (buf) {
        const uint8_t* src = (const uint8_t*)bits;
        for (int row = 0; row < L.h; row++) {
            const uint8_t* s = src + (size_t)row * (size_t)L.w * 4;
            uint8_t* d = buf + (size_t)row * (size_t)L.w * 4;
            for (int col2 = 0; col2 < L.w; col2++) {
                d[col2 * 4 + 0] = s[col2 * 4 + 2];   // R
                d[col2 * 4 + 1] = s[col2 * 4 + 1];   // G
                d[col2 * 4 + 2] = s[col2 * 4 + 0];   // B
                d[col2 * 4 + 3] = 255;               // A -- opaque, always
            }
        }
    }
    DeleteObject(dib);

    if (!buf) return false;

    // ------------------------------------------------------------------
    // AND SAY WHAT IS ACTUALLY IN IT.
    // ------------------------------------------------------------------
    // Two BATs have now gone by on a guess about where the text is being lost,
    // and a guess costs Aaron a build and an eight-minute scene each time. This
    // counts the light pixels in each text row of the bitmap it just built and
    // prints one number per line. Ten non-zero numbers mean the rasteriser is
    // right and the fault is in the GL blit; one non-zero number means the
    // rasteriser is still dropping nine lines and GL is innocent. Either way
    // the next log settles it without another guess.
    {
        char ink[160]; int n = 0;
        ink[0] = '\0';
        for (int line = 0; line < L.lines && n < (int)sizeof(ink) - 8; line++) {
            // Text rows run downward from the top in GDI terms, and the buffer
            // is bottom-up, so line 0 lives at the HIGH end of the rows.
            const int top = L.h - FP_PAD - (line + 1) * cellH;
            long lit = 0;
            for (int row = top; row < top + cellH; row++) {
                if (row < 0 || row >= L.h) continue;
                const uint8_t* d = buf + (size_t)row * (size_t)L.w * 4;
                for (int c2 = 0; c2 < L.w; c2++)
                    if (d[c2 * 4] > 120) lit++;      // brighter than the box
            }
            n += snprintf(ink + n, sizeof(ink) - n, "%s%ld",
                          line ? "," : "", lit);
        }
        Log::Field("FieldNavigation: [OVERLAY] ink per line: %s "
                   "(0 means that line never made it into the bitmap)", ink);
    }

    // PUBLISH. The show flag goes down first so the render thread cannot be
    // reading the old buffer as it is freed, then the new one goes up complete.
    s_show = false;
    uint8_t* old = s_pixels;
    s_pixels = buf;
    s_w = L.w; s_h = L.h; s_cellW = cellW; s_cellH = cellH; s_padPx = FP_PAD;
    s_drawLogged = false;
    s_probed     = false;
    s_texFresh   = false;   // the render thread re-uploads before its next draw
    s_show = true;
    if (old) free(old);

    Log::Field("FieldNavigation: [OVERLAY] shown: %d lines x %d cols -> %dx%d px "
               "(cell %dx%d)%s", L.lines, L.cols, L.w, L.h, cellW, cellH,
               L.clamped ? "  *** CLAMPED -- text did not fit ***" : "");
    return true;
}

// ---------------------------------------------------------------------------
// EmitBox -- everything the overlay draws, into WHATEVER framebuffer is bound
// when it is called. Called once per render target; see Draw().
// ---------------------------------------------------------------------------
static void EmitBox(int vw, int vh, int x, int y, int zoom, const uint8_t* px,
                    GLenum* outErr, char* backOut, size_t backN)
{
    if (backOut && backN) backOut[0] = '\0';

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)vw, 0.0, (double)vh, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

    glViewport(0, 0, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    // ---- PATH 1: the pixel transfer, unchanged from v0.65.3 --------------
    glDisable(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelZoom((GLfloat)zoom, (GLfloat)zoom);
    glRasterPos2i(x, y);
    glDrawPixels(s_w, s_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const GLenum err = glGetError();
    if (outErr) *outErr = err;

    // ---- and read back what landed IN THIS TARGET ------------------------
    if (backOut && backN && zoom == 1 && s_w > 0 && s_w <= 2048 && s_cellH > 0) {
        static uint8_t row[2048 * 4];
        int n = 0;
        const int lines = (s_h - s_padPx * 2) / s_cellH;
        for (int line = 0; line < lines && n < (int)backN - 8; line++) {
            const int by = y + (s_h - s_padPx - (line + 1) * s_cellH) + s_cellH / 2;
            glReadPixels(x, by, s_w, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
            long lit = 0;
            for (int c2 = 0; c2 < s_w; c2++) if (row[c2 * 4] > 120) lit++;
            n += snprintf(backOut + n, backN - n, "%s%ld", line ? "," : "", lit);
        }
    }

    // ---- PATH 2: the same box as a textured quad --------------------------
    // glDrawPixels needs a valid raster position, obeys glPixelZoom and is the
    // first call a translation layer implements badly. A textured quad is how
    // every overlay that has to survive an unknown renderer draws itself. Both
    // paths put identical pixels in identical places, so whichever one this
    // renderer honours, the box appears exactly once.
    if (s_tex == 0) { glGenTextures(1, &s_tex); s_texW = s_texH = 0; }
    if (s_tex != 0) {
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, s_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!s_texFresh) {
            const int tw = PotAtLeast(s_w), th = PotAtLeast(s_h);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            if (tw != s_texW || th != s_texH) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                s_texW = tw; s_texH = th;
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_w, s_h,
                            GL_RGBA, GL_UNSIGNED_BYTE, px);
            s_texFresh = true;
        }
        if (s_texW > 0 && s_texH > 0) {
            // The bitmap is bottom-up and so is this projection, so v runs the
            // same way the rows do and no flip is needed.
            const float u = (float)s_w / (float)s_texW;
            const float v = (float)s_h / (float)s_texH;
            const int x0 = x, y0 = y;
            const int x1 = x + s_w * zoom, y1 = y + s_h * zoom;
            glBegin(GL_QUADS);
              glTexCoord2f(0.0f, 0.0f); glVertex2i(x0, y0);
              glTexCoord2f(u,    0.0f); glVertex2i(x1, y0);
              glTexCoord2f(u,    v);    glVertex2i(x1, y1);
              glTexCoord2f(0.0f, v);    glVertex2i(x0, y1);
            glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        glDisable(GL_TEXTURE_2D);
    }

    // ---- THE BEACONS. See the block comment on s_beaconB. -----------------
#if FP_BEACONS
    {
        const int bx = vw - FP_BEACON - FP_BEACON_MARGIN;
        const int by = FP_BEACON_MARGIN;
        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 0.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
          glVertex2i(bx,             by);
          glVertex2i(bx + FP_BEACON, by);
          glVertex2i(bx + FP_BEACON, by + FP_BEACON);
          glVertex2i(bx,             by + FP_BEACON);
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (!s_beaconBReady) {
        for (int i = 0; i < FP_BEACON * FP_BEACON; i++) {
            s_beaconB[i * 4 + 0] = 0;    // R
            s_beaconB[i * 4 + 1] = 255;  // G
            s_beaconB[i * 4 + 2] = 255;  // B
            s_beaconB[i * 4 + 3] = 255;  // A
        }
        s_beaconBReady = true;
    }
    {
        const int bx = vw - FP_BEACON * 2 - FP_BEACON_MARGIN * 2;
        const int by = FP_BEACON_MARGIN;
        glPixelZoom(1.0f, 1.0f);
        glRasterPos2i(bx, by);
        glDrawPixels(FP_BEACON, FP_BEACON, GL_RGBA, GL_UNSIGNED_BYTE, s_beaconB);
    }
#endif  // FP_BEACONS

    glPixelZoom(1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glPopAttrib();
}

// ---------------------------------------------------------------------------
// The blit. Render thread only. No allocation, no GDI, no logging except once.
// ---------------------------------------------------------------------------
void Draw()
{
    if (!s_show) return;
    const uint8_t* px = s_pixels;
    if (!px || s_w <= 0 || s_h <= 0) return;

    ResolveGL();

    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    const int vw = vp[2], vh = vp[3];
    if (vw <= 0 || vh <= 0) return;

    const int zoom = ZoomFor(vh, s_h);
    int x = 0, y = 0;
    PlaceFor(vw, vh, s_w, s_h, zoom, &x, &y);
    s_lastX = x; s_lastY = y; s_lastZoom = zoom;

    // ------------------------------------------------------------------
    // WHOSE STATE ARE WE STANDING IN? See the block comment at the top.
    // ------------------------------------------------------------------
    GLint prevFbo = 0, prevRead = 0, prevProg = 0, prevUnit = GL_TEXTURE0, prevVao = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    // Whatever those queries did or did not answer on a driver that does not
    // know them, clear the error so glGetError below reports OUR draw.
    (void)glGetError();

    // A SHADER PROGRAM SWALLOWS FIXED-FUNCTION FRAGMENTS. With one bound, the
    // fragments glDrawPixels and glBegin produce run bgfx's fragment shader,
    // which samples ITS textures and knows nothing about our colours: the draw
    // "succeeds", glGetError stays clean, and nothing recognisable appears.
    // This is unbound for both targets and put back at the end.
    if (s_glUseProgram && prevProg != 0)       s_glUseProgram(0);
    if (s_glBindVertexArray && prevVao != 0)   s_glBindVertexArray(0);
    if (s_glActiveTexture && prevUnit != GL_TEXTURE0) s_glActiveTexture(GL_TEXTURE0);
    (void)glGetError();

    // ------------------------------------------------------------------
    // DRAW INTO BOTH CANDIDATE TARGETS.
    // ------------------------------------------------------------------
    // We do not know -- and from inside this hook cannot know -- whether the
    // frame that reaches the screen is bgfx's offscreen target (composited
    // during SwapBuffers) or the window's own back buffer. Both stories fit
    // every observation to date, and picking one and being wrong costs another
    // BAT. Drawing the same pixels at the same coordinates into both costs one
    // extra blit of a 372x240 image per frame and cannot be wrong: whichever
    // target is the one presented, the box is in it, and the other is either
    // discarded or overwritten.
    GLenum errA = GL_NO_ERROR, errB = GL_NO_ERROR;
    char backA[160], backB[160];
    backA[0] = backB[0] = '\0';
    const bool wantLog = !s_drawLogged;

    // Target A: exactly what bgfx had bound. This is what v0.65.3..v0.65.6 did.
    EmitBox(vw, vh, x, y, zoom, px, &errA, wantLog ? backA : nullptr, sizeof backA);

    // Target B: the window's own framebuffer, if that is a different thing and
    // we have the entry point to select it.
    bool didB = false;
    if (s_glBindFramebuffer && (prevFbo != 0 || prevRead != 0)) {
        s_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        (void)glGetError();
        // The texture object is per-CONTEXT, not per-framebuffer, so the
        // upload target A just made is still bound and current here.
        EmitBox(vw, vh, x, y, zoom, px, &errB, wantLog ? backB : nullptr, sizeof backB);
        didB = true;
    }

    // ------------------------------------------------------------------
    // Put bgfx's state back exactly as we found it.
    // ------------------------------------------------------------------
    // Restore the draw and read bindings separately when bgfx had them split;
    // GL_FRAMEBUFFER sets both, which would quietly merge two bindings into one.
    if (didB && s_glBindFramebuffer) {
        if (prevRead != prevFbo) {
            s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevFbo);
            s_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        } else {
            s_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        }
    }
    if (s_glActiveTexture && prevUnit != GL_TEXTURE0) s_glActiveTexture((GLenum)prevUnit);
    if (s_glBindVertexArray && prevVao != 0)          s_glBindVertexArray((GLuint)prevVao);
    if (s_glUseProgram && prevProg != 0)              s_glUseProgram((GLuint)prevProg);
    (void)glGetError();

    // ONCE per Show(), because this runs sixty times a second.
    if (!s_drawLogged) {
        s_drawLogged = true;
        const char* ver = (const char*)glGetString(GL_VERSION);
        const char* ren = (const char*)glGetString(GL_RENDERER);
        Log::Field("FieldNavigation: [OVERLAY] first blit: %dx%d at (%d,%d) zoom %d "
                   "in a %dx%d viewport, glGetError=0x%04X%s",
                   s_w, s_h, x, y, zoom, vw, vh, (unsigned)errA,
                   errA == GL_NO_ERROR ? "" : "  *** the GL call was rejected ***");
        Log::Field("FieldNavigation: [OVERLAY] read back from the frame buffer: %s "
                   "(compare with 'ink per line' -- if they disagree, the blit is "
                   "the problem, not the bitmap)", backA[0] ? backA : "(not taken)");
        if (didB) {
            Log::Field("FieldNavigation: [OVERLAY] and read back from the WINDOW's own "
                       "framebuffer: %s (glGetError=0x%04X)",
                       backB[0] ? backB : "(not taken)", (unsigned)errB);
        }
        Log::Field("FieldNavigation: [OVERLAY] the state we drew into: "
                   "draw fbo=%d read fbo=%d program=%d texture unit=0x%04X vao=%d; "
                   "entry points bindFramebuffer=%s useProgram=%s activeTexture=%s "
                   "bindVertexArray=%s; texture id=%u %dx%d; GL %s / %s",
                   (int)prevFbo, (int)prevRead, (int)prevProg,
                   (unsigned)prevUnit, (int)prevVao,
                   s_glBindFramebuffer ? "yes" : "NO",
                   s_glUseProgram      ? "yes" : "NO",
                   s_glActiveTexture   ? "yes" : "NO",
                   s_glBindVertexArray ? "yes" : "NO",
                   (unsigned)s_tex, s_texW, s_texH,
                   ver ? ver : "?", ren ? ren : "?");
        if (prevFbo != 0 || prevProg != 0) {
            Log::Field("FieldNavigation: [OVERLAY] *** and THAT is very likely the "
                       "answer: %s%s%s was bound, which is where three builds of "
                       "overlay went ***",
                       prevFbo  != 0 ? "an offscreen framebuffer" : "",
                       (prevFbo != 0 && prevProg != 0) ? " and " : "",
                       prevProg != 0 ? "a shader program" : "");
        }
    }
}

// ---------------------------------------------------------------------------
// What the F11 capture actually contains. See the header for why this exists.
// ---------------------------------------------------------------------------
void ProbeCapture(const unsigned char* bgr, int w, int h, int stride)
{
    if (!s_show || s_probed) return;
    if (!bgr || w <= 0 || h <= 0 || stride <= 0) return;
    if (s_cellH <= 0 || s_w <= 0 || s_h <= 0) return;
    s_probed = true;

    const int x = s_lastX, y = s_lastY, zoom = s_lastZoom;

    // The same per-line ink measure the rasteriser and the read-back report,
    // taken this time from the bytes that are about to become the PNG.
    char ink[160]; int n = 0; ink[0] = '\0';
    const int lines = (s_h - s_padPx * 2) / s_cellH;
    for (int line = 0; line < lines && n < (int)sizeof(ink) - 8; line++) {
        const int gy = y + zoom * (s_h - s_padPx - (line + 1) * s_cellH
                                   + s_cellH / 2);
        long lit = 0;
        if (gy >= 0 && gy < h) {
            const unsigned char* row = bgr + (size_t)gy * (size_t)stride;
            for (int c = 0; c < s_w * zoom; c++) {
                const int px = x + c;
                if (px < 0 || px >= w) continue;
                if (row[px * 3 + 2] > 120) lit++;   // R of BGR
            }
        }
        n += snprintf(ink + n, sizeof(ink) - n, "%s%ld", line ? "," : "", lit);
    }

    // And the two beacons, by the colour at their centres. Reads -1/-1/-1 with
    // FP_BEACONS off, which is correct: nothing was drawn there to measure.
    int ar = -1, ag = -1, ab = -1, br = -1, bg2 = -1, bb = -1;
#if FP_BEACONS
    {
        const int ax = w - FP_BEACON - FP_BEACON_MARGIN + FP_BEACON / 2;
        const int ay = FP_BEACON_MARGIN + FP_BEACON / 2;
        if (ax >= 0 && ax < w && ay >= 0 && ay < h) {
            const unsigned char* p = bgr + (size_t)ay * (size_t)stride + ax * 3;
            ab = p[0]; ag = p[1]; ar = p[2];
        }
        const int bx = w - FP_BEACON * 2 - FP_BEACON_MARGIN * 2 + FP_BEACON / 2;
        if (bx >= 0 && bx < w && ay >= 0 && ay < h) {
            const unsigned char* p = bgr + (size_t)ay * (size_t)stride + bx * 3;
            bb = p[0]; bg2 = p[1]; br = p[2];
        }
    }
#endif

    Log::Field("FieldNavigation: [OVERLAY] IN THE CAPTURE: ink per line %s; "
               "beacon A (quad, want magenta 255/0/255) = %d/%d/%d; "
               "beacon B (glDrawPixels, want cyan 0/255/255) = %d/%d/%d; "
               "box %dx%d at (%d,%d) zoom %d in a %dx%d frame",
               ink, ar, ag, ab, br, bg2, bb,
               s_w, s_h, x, y, zoom, w, h);
}

} // namespace FieldOverlay
