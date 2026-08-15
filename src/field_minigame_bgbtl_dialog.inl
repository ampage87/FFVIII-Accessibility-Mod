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
static const uintptr_t ADDR_CLOSE_WINDOW      = 0x0049FBF0;
static const uintptr_t ADDR_FIELD_CTX_PTR     = 0x00B8EE90;   // -> +0xD3/+0xD4

typedef void     (__cdecl *SetWindowObject_t)(int winId, const char* text);
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
static uint8_t EncodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x45 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x5F + (c - 'a'));
    if (c >= '0' && c <= '9') return (uint8_t)(0x21 + (c - '0'));
    switch (c) {
        case ' ':  return 0x20;
        case '!':  return 0x2E;
        case '?':  return 0x2F;
        case '.':  return 0x3B;
        case ',':  return 0x3C;
        case '\'': return 0x40;
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

// Opens the box. Returns false and logs if anything looks wrong -- the briefing
// is spoken either way, so a window that will not open costs nothing but the
// picture.
static bool OpenBriefDialog(const char* ascii)
{
    if (s_dlgOpen) return true;
    if (!ascii || !*ascii) return false;
    __try {
        const size_t n = EncodeWrapped(ascii, BRIEF_COLS, s_dlgText, sizeof(s_dlgText));
        if (n == 0) return false;

        ((SetWindowObject_t)ADDR_SET_WINDOW_OBJECT)(BRIEF_WINDOW, (const char*)s_dlgText);

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

        // Centred across, but PARKED AT THE TOP: the game draws its own
        // "W Punch / A Block / X Kick" legend at the lower left and the two
        // fighters' HP bars along the bottom, and a centred box lands on the
        // first of those. Then held inside the screen by AMES's own rule.
        int16_t x = (int16_t)((SCREEN_W - w) / 2);
        int16_t y = 8;
        if (x + w >= SCREEN_W) x = (int16_t)(SCREEN_W - w);
        if (y + h >= SCREEN_H) y = (int16_t)(SCREEN_H - h);
        if (x < 8) x = 8;
        if (y < 8) y = 8;

        const int16_t rect[4] = { x, y, w, h };
        ((SetWinGeometry_t)ADDR_SET_WIN_GEOMETRY)(BRIEF_WINDOW, rect);
        ((WinIdOnly_t)ADDR_OPEN_WINDOW)(BRIEF_WINDOW);
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
