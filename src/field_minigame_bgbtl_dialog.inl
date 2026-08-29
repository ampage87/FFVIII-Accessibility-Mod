// field_minigame_bgbtl_dialog.inl -- v0.20.123 (#minigame-bgbtl)
//
// The Game Controls screen, drawn in one of FF8's own dialog windows so that
// low-vision players and anyone watching can read what the mod is saying.
// Included from field_minigame_bgbtl.inl; part of the FieldNavigation
// namespace. Do not compile independently.
//
// ---------------------------------------------------------------------------
// HOW A WINDOW IS PUT ON SCREEN, READ OFF THE AMES OPCODE
//
// Opcode 0x65 (AMES -- the auto-positioning, auto-sizing message) at 0x005291E0
// is the whole recipe, and it is the one to copy because AMES is the variant
// that sizes the box to its text:
//
//     text  = field_get_dialog_string(...)          ; 0x00530750
//     set_window_object(winId, text)                ; 0x004A0410
//     size  = measure_text(text)                    ; 0x004A0EC0
//     w = (size & 0xFFFF) + 0x10                    ; +16 for the border
//     h = (size >> 16)   + 0x11                     ; +17
//     clamp x so x + w < 0x130, y so y + h < 0xE0, both to a minimum of 8
//     set_window_geometry(winId, rect{x,y,w,h})     ; 0x004A07A0
//     open_window(winId)                            ; 0x004A0620
//     set_current_window(winId)                     ; 0x0049FD50
//     [0x00B8EE90] + 0xD3 |= (1 << winId)           ; the "open" masks
//     [0x00B8EE90] + 0xD4 |= (1 << winId)
//
// and WINCLOSE (opcode 0x4C, 0x00529B60) is the reverse: 0x0049FBF0(winId)
// starts the close, then the same two bits are cleared.
//
// **The size comes from the game's own text measurer**, so "sized properly so
// all the text is visible" is not something this file estimates -- 0x004A0EC0
// walks the string with the real font metrics and returns the width in its low
// word and the height in its high word. All this code has to do is wrap the
// text to a sane column first.
//
// ---------------------------------------------------------------------------
// WHY THE BRIEFING NO LONGER FREEZES field_main
//
// It could not. Window rendering is 0x004A0880, and the call chain is
// field_main -> 0x00471010 -> 0x0052BC00 -> 0x004A0880 -- so the one-byte RET
// that has paused this scene since v0.20.106 also stops any window from being
// drawn, and the state machine that opens it from ever advancing.
//
// The briefing now uses the mechanism the F9 skip already proved in the field:
// **veto the soldier's attack REQs and pin both HP values.** Nothing hits
// Squall, nothing makes a sound, and the game keeps running -- which is what
// lets the box appear, and incidentally lets the key learner read the real
// button word instead of the engine-side stand-in it needed while frozen.
//
// The cost is that the movie behind the fight keeps playing, so a very long
// briefing eats into the round. That is bounded below by EndBriefingIfLate().

static const uintptr_t ADDR_SET_WINDOW_OBJECT = 0x004A0410;
static const uintptr_t ADDR_MEASURE_TEXT      = 0x004A0EC0;
static const uintptr_t ADDR_SET_WIN_GEOMETRY  = 0x004A07A0;
static const uintptr_t ADDR_OPEN_WINDOW       = 0x004A0620;
static const uintptr_t ADDR_SET_CUR_WINDOW    = 0x0049FD50;
static const uintptr_t ADDR_FIELD_CTX_PTR     = 0x00B8EE90;   // -> +0xD3/+0xD4

// ---------------------------------------------------------------------------
// v0.67.0: THE TYPEWRITER, AND HOW TO SWITCH IT OFF
// ---------------------------------------------------------------------------
// Aaron, on whether the mod's Game Controls screen looks like the rest of the
// game: *"I don't think it looks very good if our injected dialogs / text
// doesn't look the same as the rest of the game."* Quite right -- and the way
// to make it look identical is not to imitate FF8's font, it is to keep using
// FF8's own window, which this file already does, and stop it being caught
// half-typed.
//
// The window state array is `0x01D2B330 + winId * 0x3C`, 8 slots. Two fields
// matter here, both proven out of the exe:
//
//   +0x10  i16  TEXT SPEED, 4.12 fixed. The state machine accumulates it at
//               0x004A000A and emits a character when the accumulator passes
//               0x1000 -- except that 0x004A0013 SHORT-CIRCUITS THE TEST WHEN
//               THE SPEED IS ZERO, and 0x004A010A..0x004A0113 then picks the
//               "consume the next character" state instead of "re-check the
//               gate", re-entering the dispatcher inside the same frame. So
//               speed 0 does not mean "very fast": it means the whole string
//               is consumed in ONE update and the box is complete on the tick
//               it opens.
//   +0x28  u8   MESSAGE COMPLETE, set at 0x004A0144 when the terminator is
//               reached and cleared by every set_window_object. This is the
//               engine's own predicate -- 0x004A06E0 is a one-line accessor
//               for it, and the field MESSAGE opcode at 0x00529956 polls it
//               exactly like this to decide when a message is done.
//
// Speed 0 is IN-DOMAIN, not invented: it is what the game's own {SPEED 1} text
// opcode writes at 0x004A00AC. And 0x0049FBC0 is the engine's setter, which
// the field itself calls for all eight windows at 0x0052B7D8 on every field
// init -- which is also why the mod re-applies it on every open rather than
// once: a map load stomps it back to the config value.
static const uintptr_t ADDR_SET_TEXT_SPEED    = 0x0049FBC0;
static const uintptr_t ADDR_OPEN_WINDOW_FAST  = 0x004A0640;   // +0x1E = 0x1000
static const uintptr_t ADDR_MSG_COMPLETE      = 0x004A06E0;   // -> byte +0x28
static const uintptr_t WINDOW_STATE_BASE      = 0x01D2B330;
static const uintptr_t WINDOW_STATE_STRIDE    = 0x3C;
static const uintptr_t WINDOW_OFF_COMPLETE    = 0x28;

// THE CLOSER WAS NEVER A CLOSER. 0x0049FBF0 returns the i16 at +0x1C -- the
// window's open scale, 0 shut and 0x1000 fully open (0x0049FBFA). Calling it
// closed nothing; what actually took the box off screen was clearing the two
// ctx bits below it, which is why this looked like it worked. The engine's
// closers are 0x004A0660 (animated, +0x1E = -0x200) and 0x004A0680 (immediate,
// -0x1000). Use the immediate one: this box is dismissed by a keypress the
// player has already made, and an eight-frame shrink is eight frames of a
// scene he is trying to get on with.
static const uintptr_t ADDR_CLOSE_WINDOW      = 0x004A0680;

typedef void     (__cdecl *SetWindowObject_t)(int winId, const char* text);
typedef void     (__cdecl *SetTextSpeed_t)(int winId, int16_t speed);
typedef int      (__cdecl *MsgComplete_t)(int winId);
typedef uint32_t (__cdecl *MeasureText_t)(const char* text);
typedef void     (__cdecl *SetWinGeometry_t)(int winId, const int16_t* rect);
typedef void     (__cdecl *WinIdOnly_t)(int winId);
typedef int      (__cdecl *CloseWindow_t)(int winId);

// The scene's own legend uses window 4 (`win[4] Speaking: "Punch Block Kick"`
// in every dialog log of this fight), and the field rarely opens more than two
// at once. 7 is the top of the engine's 8-slot array and has never been seen
// occupied here.
static const int BRIEF_WINDOW = 7;

// FF8 screen space, from AMES's own clamps.
static const int16_t SCREEN_W = 0x130;
static const int16_t SCREEN_H = 0xE0;

static bool     s_dlgOpen = false;
static uint8_t  s_dlgText[1024];

// --------------------------------------------------------------- encoding
//
// The inverse of ff8_text_decode.h's table, restricted to what the briefing
// actually uses. Anything unmappable becomes a space rather than a glyph the
// player would have to puzzle over.
//
//   0x00        end of string        0x02        newline
//   0x20        space                0x21-0x2A   '0'-'9'
//   0x2E '!'    0x2F '?'             0x3B '.'    0x3C ','
//   0x40        apostrophe           0x45-0x5E   'A'-'Z'
//   0x5F-0x78   'a'-'z'
// v0.67.1: THE PUNCTUATION THE ENCODER USED TO EAT.
//
// The 14:11 capture is the first picture of this box with all ten lines in it,
// and it shows the cost of `default: return 0x20`: "Boost  hold W." for
// "Boost: hold W.", "4x speed  burns boost fuel." for the semicolon version,
// ` Centred  means let go.` where the quotes should be, and "Enter start
// repeat F9 skip" with the slash gone. Four characters silently turned into
// spaces because they were not in a hand-written switch.
//
// Every code below is read out of the mod's OWN decode table -- the 224-entry
// glyph grid in ff8_text_decode.cpp that already turns these bytes back into
// text everywhere else in the mod -- so the encoder and the decoder cannot
// disagree about a character. The four already here (0x2E 0x2F 0x3B 0x3C 0x40)
// were right; they were simply most of what there was.
//
// The double quote is the one exception: the glyph grid has no entry for it,
// because FF8 uses the Japanese quote pair. The decoder maps 0x36/0x37 and
// 0x3E/0x3F all to '"', so encoding to 0x3E round-trips through the mod's own
// reader and renders as the game's own opening quote.
//
// UNKNOWN CHARACTERS STILL BECOME SPACES, and that is still right -- a glyph
// the font does not have cannot be drawn -- but the set of characters that
// falls into it is now the accented letters and the box-drawing oddities,
// rather than the colon.
static uint8_t EncodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x45 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x5F + (c - 'a'));
    if (c >= '0' && c <= '9') return (uint8_t)(0x21 + (c - '0'));
    switch (c) {
        case ' ':  return 0x20;
        case '!':  return 0x2E;
        case '%':  return 0x2B;
        case '/':  return 0x2C;
        case ':':  return 0x2D;
        case '?':  return 0x2F;
        case '+':  return 0x31;
        case '-':  return 0x32;
        case '=':  return 0x33;
        case '*':  return 0x34;
        case '&':  return 0x35;
        case '(':  return 0x38;
        case ')':  return 0x39;
        case '.':  return 0x3B;
        case ',':  return 0x3C;
        case '"':  return 0x3E;   // the grid is blank here; the decoder is not
        case '\'': return 0x40;
        case '#':  return 0x41;
        case '$':  return 0x42;
        case '_':  return 0x44;
        case '[':  return 0xA9;
        case ']':  return 0xAA;
        case ';':  return 0xB5;
        case '<':  return 0xCA;
        case '>':  return 0xCB;
        case '\n': return 0x02;
        default:   return 0x20;
    }
}

// Word-wrap to `cols` characters and encode. FF8's window is 320 px wide and
// the font is roughly 8 px per character, so anything past about 34 columns
// would be clamped by AMES's own `x + w < 0x130` rule and clipped -- wrapping
// here is what keeps every line inside the box the measurer then sizes.
static size_t EncodeWrapped(const char* ascii, int cols, uint8_t* out, size_t cap)
{
    size_t o = 0;
    int col = 0;
    const char* p = ascii;
    while (*p && o + 2 < cap) {
        // measure the next word
        const char* w = p;
        while (*w == ' ') w++;
        const char* e = w;
        while (*e && *e != ' ' && *e != '\n') e++;
        const int len = (int)(e - w);
        if (*w == '\n') { out[o++] = 0x02; col = 0; p = w + 1; continue; }
        if (len == 0) break;
        if (col > 0 && col + 1 + len > cols) { out[o++] = 0x02; col = 0; }
        else if (col > 0)                   { out[o++] = 0x20; col++; }
        for (const char* q = w; q < e && o + 2 < cap; q++) { out[o++] = EncodeChar(*q); col++; }
        p = e;
    }
    out[o] = 0x00;
    return o;
}

// ----------------------------------------------------------------- driving

static uint8_t* FieldCtx()
{
    __try { return *(uint8_t**)ADDR_FIELD_CTX_PTR; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void SetWindowOpenBits(int winId, bool on)
{
    uint8_t* ctx = FieldCtx();
    if (!ctx) return;
    const uint8_t bit = (uint8_t)(1u << winId);
    __try {
        if (on) { ctx[0xD3] |= bit;  ctx[0xD4] |= bit;  }
        else    { ctx[0xD3] &= (uint8_t)~bit; ctx[0xD4] &= (uint8_t)~bit; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// IS SLOT 7 ACTUALLY FREE HERE?
//
// v0.67.2: this facility started as one screen in one fight, where window 7 had
// been observed unoccupied. It is now the house style for every Game Controls
// screen -- the space rescue, the Garden battle, the Trabia dragon -- and the
// next scene to use it is a scene nobody has checked. Opening a window the game
// is already using would take the game's own text off the screen and put ours
// where it was, which is a worse failure than not drawing at all: the player
// loses information he was supposed to have and nothing says so.
//
// ctx+0xD3 is the engine's own per-window open mask, the same byte this file
// sets and clears. If the bit is already up, somebody else owns the slot.
static bool BriefWindowFree()
{
    uint8_t* ctx = FieldCtx();
    if (!ctx) return false;
    __try {
        return (ctx[0xD3] & (uint8_t)(1u << BRIEF_WINDOW)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------------------
// WHERE THE BOX GOES, GIVEN WHAT IS ALREADY ON SCREEN (v0.67.3)
// ---------------------------------------------------------------------------
// Aaron's 15:05 shot of the Trabia dragon: the box opened in the game's own
// window exactly as intended -- and the scene's OWN two legend windows, "A to
// defend!" and "W to attack!", sat on top of its left-hand third and hid the
// first three lines. Slot 7 really was free; the collision was positional, not
// a question of ownership, and no amount of checking ctx+0xD3 would have caught
// it.
//
// A FIXED CORNER CANNOT WORK. The Garden battle parks its box at the top
// because that scene draws its legend at the lower left and HP bars along the
// bottom. Trabia puts its legends at the TOP left. Any constant is right for one
// scene and wrong for the next, and the next scene is always the one nobody has
// looked at.
//
// So the box asks. Every window carries its own rectangle at +0x00 as four
// i16s (set by set_window_geometry at 0x004A07E4) and its open flag at +0x16
// (0x004A0631), so the mod can read what is on screen and put itself somewhere
// else. The candidates are tried in order and the first clear one wins; if
// every one collides -- a scene with windows in all four corners -- the least
// overlapped is used and the log says so, because a box that is partly covered
// still beats no box.
struct WinRect { int x, y, w, h; };

static bool RectsOverlap(const WinRect& a, const WinRect& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static int OverlapArea(const WinRect& a, const WinRect& b)
{
    const int x0 = (a.x > b.x) ? a.x : b.x;
    const int y0 = (a.y > b.y) ? a.y : b.y;
    const int x1 = (a.x + a.w < b.x + b.w) ? a.x + a.w : b.x + b.w;
    const int y1 = (a.y + a.h < b.y + b.h) ? a.y + a.h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0) return 0;
    return (x1 - x0) * (y1 - y0);
}

// PURE, so the probe can check it without a Trabia Canyon. `occupied` is every
// other window that is currently on screen. Returns the chosen top-left and,
// through `outCost`, how much of the box is still covered (0 = clear).
static void PlaceBriefBox(int w, int h, const WinRect* occupied, int nOccupied,
                          int* outX, int* outY, int* outCost, int* outChoice)
{
    const int maxX = (int)SCREEN_W - w - 1;
    const int maxY = (int)SCREEN_H - h - 1;
    int cx = ((int)SCREEN_W - w) / 2;   if (cx < 8) cx = 8;   if (cx > maxX) cx = maxX;
    int by = (int)SCREEN_H - h - 8;     if (by < 8) by = 8;   if (by > maxY) by = maxY;
    int rx = (int)SCREEN_W - w - 8;     if (rx < 8) rx = 8;   if (rx > maxX) rx = maxX;
    int cy = ((int)SCREEN_H - h) / 2;   if (cy < 8) cy = 8;   if (cy > maxY) cy = maxY;

    // Top first, because that is where the Garden battle has always wanted it
    // and where a box is least likely to cover a health bar.
    const WinRect cand[5] = {
        { cx, 8,  w, h },   // 0 centred at the top   -- the old behaviour
        { cx, by, w, h },   // 1 centred at the bottom
        { rx, 8,  w, h },   // 2 hard right at the top
        { 8,  by, w, h },   // 3 hard left at the bottom
        { cx, cy, w, h },   // 4 dead centre
    };

    int best = 0, bestCost = -1;
    for (int i = 0; i < 5; i++) {
        int cost = 0;
        for (int j = 0; j < nOccupied; j++) cost += OverlapArea(cand[i], occupied[j]);
        if (cost == 0) { best = i; bestCost = 0; break; }
        if (bestCost < 0 || cost < bestCost) { best = i; bestCost = cost; }
    }
    if (outX) *outX = cand[best].x;
    if (outY) *outY = cand[best].y;
    if (outCost) *outCost = bestCost < 0 ? 0 : bestCost;
    if (outChoice) *outChoice = best;
}

// Every OTHER window that is currently on screen. Reads the rectangle at +0x00
// and the open flag at +0x16 out of the same array BriefDialogComplete uses.
static int CollectOpenWindows(WinRect* out, int cap)
{
    int n = 0;
    __try {
        for (int id = 0; id < 8 && n < cap; id++) {
            if (id == BRIEF_WINDOW) continue;
            const uintptr_t w = WINDOW_STATE_BASE + WINDOW_STATE_STRIDE * (uintptr_t)id;
            if (*(volatile const uint8_t*)(w + 0x16) == 0) continue;   // not open
            const int16_t* r = (const int16_t*)w;
            WinRect rc = { r[0], r[1], r[2], r[3] };
            if (rc.w <= 0 || rc.h <= 0) continue;
            out[n++] = rc;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
    return n;
}

// Opens the box. Returns false and logs if anything looks wrong -- the briefing
// is spoken either way, so a window that will not open costs nothing but the
// picture.
static bool OpenBriefDialog(const char* ascii)
{
    if (s_dlgOpen) return true;
    if (!ascii || !*ascii) return false;
    if (!BriefWindowFree()) {
        Log::Field("FieldNavigation: [BGBTL-DLG] window %d is already open in this "
                   "scene -- NOT taking it. The briefing is spoken; taking a window "
                   "the game is using would replace its text with ours and say "
                   "nothing about it", BRIEF_WINDOW);
        return false;
    }
    __try {
        const size_t n = EncodeWrapped(ascii, BRIEF_COLS, s_dlgText, sizeof(s_dlgText));
        if (n == 0) return false;

        ((SetWindowObject_t)ADDR_SET_WINDOW_OBJECT)(BRIEF_WINDOW, (const char*)s_dlgText);

        // ...and AT ONCE, before anything can type a character. See the block
        // comment on ADDR_SET_TEXT_SPEED. Applied after set_window_object
        // because that call clears +0x28 and would undo an earlier write.
        ((SetTextSpeed_t)ADDR_SET_TEXT_SPEED)(BRIEF_WINDOW, 0);

        const uint32_t sz = ((MeasureText_t)ADDR_MEASURE_TEXT)((const char*)s_dlgText);
        int16_t w = (int16_t)((sz & 0xFFFF) + 0x10);
        // v0.20.125: +8 on top of AMES's own +0x11. In Aaron's 15:24 shot the
        // measurer returned 210x108 for seven lines and the last one sat right
        // on the bottom border. The measurer is not wrong -- it reports the
        // text -- but AMES's border allowance leaves nothing under a final line
        // that reaches the full height, so this adds a line's worth of slack.
        int16_t h = (int16_t)(((int32_t)sz >> 16) + 0x11 + 8);
        if (w < 8) w = 8;
        if (h < 8) h = 8;
        if (w > SCREEN_W - 16) w = (int16_t)(SCREEN_W - 16);
        if (h > SCREEN_H - 16) h = (int16_t)(SCREEN_H - 16);

        // WHERE IT GOES depends on what is already on screen. See PlaceBriefBox.
        static const char* WHERE[5] = { "centred at the top", "centred at the bottom",
                                        "hard right at the top", "hard left at the bottom",
                                        "dead centre" };
        WinRect busy[8];
        const int nBusy = CollectOpenWindows(busy, 8);
        int px = 8, py = 8, cost = 0, choice = 0;
        PlaceBriefBox((int)w, (int)h, busy, nBusy, &px, &py, &cost, &choice);
        int16_t x = (int16_t)px;
        int16_t y = (int16_t)py;
        if (x + w >= SCREEN_W) x = (int16_t)(SCREEN_W - w);
        if (y + h >= SCREEN_H) y = (int16_t)(SCREEN_H - h);
        if (x < 8) x = 8;
        if (y < 8) y = 8;
        Log::Field("FieldNavigation: [BGBTL-DLG] %d other window%s on screen -> placed "
                   "%s%s", nBusy, nBusy == 1 ? "" : "s", WHERE[choice],
                   cost ? "  *** still overlapped -- every corner was taken ***" : "");

        const int16_t rect[4] = { x, y, w, h };
        ((SetWinGeometry_t)ADDR_SET_WIN_GEOMETRY)(BRIEF_WINDOW, rect);
        // The instant-open variant. 0x004A0620 animates the frame open over
        // eight frames (+0x1E = 0x200) and no text is typed until the scale
        // reaches 0x1000 (0x0049FFAE), so the ordinary opener would put eight
        // frames of empty box between the screen appearing and it being
        // readable -- and in the space rescue those are eight frames of a
        // scene that is still running.
        ((WinIdOnly_t)ADDR_OPEN_WINDOW_FAST)(BRIEF_WINDOW);
        ((WinIdOnly_t)ADDR_SET_CUR_WINDOW)(BRIEF_WINDOW);
        SetWindowOpenBits(BRIEF_WINDOW, true);

        s_dlgOpen = true;
        Log::Field("FieldNavigation: [BGBTL-DLG] window %d opened, %u encoded bytes, "
                   "measured %dx%d -> box %dx%d at (%d,%d)",
                   BRIEF_WINDOW, (unsigned)n, (int)(sz & 0xFFFF), (int)((int32_t)sz >> 16),
                   (int)w, (int)h, (int)x, (int)y);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [BGBTL-DLG] exception opening the window -- "
                   "the briefing is spoken anyway");
        s_dlgOpen = false;
        return false;
    }
}

// v0.63.0: the space rescue's probe asks whether the window is really up, so
// the Game Controls assertions rest on the window state rather than on the fact
// that something was spoken.
static bool BriefDialogOpen() { return s_dlgOpen; }

// The engine's own "this window has finished typing" flag, read straight out of
// the window state rather than through 0x004A06E0 -- one guarded byte read is
// cheaper and safer from the mod thread than a call into the engine.
static bool BriefDialogComplete()
{
    if (!s_dlgOpen) return false;
    __try {
        const uintptr_t a = WINDOW_STATE_BASE + WINDOW_STATE_STRIDE * (uintptr_t)BRIEF_WINDOW
                          + WINDOW_OFF_COMPLETE;
        return *(volatile const uint8_t*)a != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void CloseBriefDialog()
{
    if (!s_dlgOpen) return;
    s_dlgOpen = false;
    __try {
        ((CloseWindow_t)ADDR_CLOSE_WINDOW)(BRIEF_WINDOW);
        SetWindowOpenBits(BRIEF_WINDOW, false);
        Log::Field("FieldNavigation: [BGBTL-DLG] window %d closed", BRIEF_WINDOW);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [BGBTL-DLG] exception closing the window");
    }
}
