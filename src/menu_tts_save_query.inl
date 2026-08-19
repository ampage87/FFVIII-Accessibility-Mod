// menu_tts_save_query.inl -- v0.23.4 (#120)
//
// The Save screen's overwrite confirmation: "Data exists.  Overwrite?" with a
// Yes/No cursor. Until now the mod went silent the moment it opened -- the
// block list stopped answering, nothing named the box, and the only way to know
// which option the cursor was on was to press something and find out.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE. Included AFTER menu_tts_save.inl (it
// resets that file's block-cursor latch) and AFTER menu_magic_model.inl (it
// reuses MagicTextToGlyphs). Do NOT compile standalone.
//
// ---------------------------------------------------------------------------
// WHAT THE DISASSEMBLY SAYS  (FF8_EN.exe, Steam 2013)
//
// The Save screen is menu module index 6 of the submenu dispatch table at
// 0x00B87ED8: creator 0x004E6740, state machine 0x004E3090, draw 0x004E5550.
// The creator registers the state machine through the pool allocator
// 0x004BE540, so module +0x08 == 0x004E3090 identifies it uniquely -- the same
// identification menu_tts_magic.inl uses for Magic, and the reason this file
// does not care whether the screen came from the field (pool slot 1, the
// historical "mode 1" offsets) or from the in-game menu (slot 2, "mode 6").
//
// Its 84-state jump table lives at 0x004E5294. Block selection is state 0x21;
// on confirm it reads the per-block status table at 0x01D8CB30 (one byte per
// block, 0 = empty) and branches at 0x004E3880:
//
//     status == 0        -> state 0x38   save straight into the empty block
//     status == 1 or 2   -> state 0x36   THE OVERWRITE CONFIRMATION
//     anything else      -> error beep
//
// State 0x36 (0x004E3B0E) opens the box in a single frame:
//
//     mov  byte [esi+0x48], 4            ; panel id = the confirmation
//     mov  byte [esi+0x4F], 1            ; cursor defaults to the SECOND option
//     call 0x004BD630 (1, 5, 5, 0)       ; "Data exists.  Overwrite?"
//     call 0x004C2B10 (text, 0x54)       ; open the generic yes/no query window
//     mov  word [esi+0x10], 0x37         ; -> the input loop
//
// State 0x37 (0x004E3B3D) is the loop. It runs the two-entry cursor helper
// 0x004C0A30(keys, 2, cursor), writes the result back to +0x4F, and on confirm
// does this at 0x004E4C49:
//
//     dl = [esi+0x4F] ; neg dl ; sbb edx,edx ; and edx,0xFFFFFFE8 ; add edx,0x38
//
// which is 0x38 when the cursor is 0 and 0x20 when it is anything else. So
// **cursor 0 is Yes (proceed to the save) and cursor 1 is No (back to the block
// list)**, and the game arms it on No. Cancel (0x004E4C5D) also returns 0x20.
//
// 0x004E3B37 is the ONLY instruction in the whole state machine that writes
// state 0x37, so `state == 0x37` on its own means "the overwrite confirmation
// is up and taking input". The panel byte is checked as well, but only as a
// belt-and-braces: it is set by the same handler two instructions earlier.
//
// ---------------------------------------------------------------------------
// WHERE THE WORDS COME FROM
//
// Not from a table in this file. 0x004C2B10 tail-calls 0x004C2A20, which parks
// three pointers in globals when the window opens:
//
//     0x01D77300  the prompt
//     0x01D772F0  option 0   (null arg -> getter(0, 0, 0x2F, 0), "Yes")
//     0x01D772E0  option 1   (null arg -> getter(0, 0, 0x30, 0), "No")
//
// Each is a pointer straight into loaded menu text -- the getter at 0x004BD630
// returns a pointer rather than copying -- so they are long-lived bytes safe to
// read from the mod's thread, exactly like the Magic help bar at module +0x24.
// They are TEXT-STREAM bytes, `glyph + 0x20`, so they must be shifted down
// before FF8TextDecode::DecodeMenuText sees them (v0.22.2's "AaI'UEIOE" bug).
//
// Reading them instead of hardcoding matters for a reason beyond tidiness: a
// blind player and a sighted player then hear and see the same sentence, and a
// localisation change cannot silently desync the two. The literals below are
// only a fallback for a fault or a null.
//
// Offline provenance for the fallback text: mngrp.bin section 1 (file offset
// 0x800), bank 5 -- the Save screen's own string bank -- entry 5 is
// "Data exists.  Overwrite?", entry 6 "Yes", entry 7 "No". The same bank holds
// "Choose block to save" (entry 4) and "Saving data" (entry 9), which is what
// confirms the bank identification rather than assuming it.
// ---------------------------------------------------------------------------

// ---- the module pool (same pool menu_tts_magic.inl walks) ------------------
static const uintptr_t SQ_POOL_BASE     = 0x01D76BC8;
static const uintptr_t SQ_POOL_END      = 0x01D77078;   // base + 10 * 0x78
static const uintptr_t SQ_LIST_HEAD     = 0x01D76B48;
static const uint32_t  SQ_SAVE_STATE_FN = 0x004E3090;   // dispatch table 0x00B87ED8 index 6

// ---- module field offsets --------------------------------------------------
static const int SQO_UPDATE_FN = 0x08;
static const int SQO_STATE     = 0x10;
static const int SQO_PANEL     = 0x48;   // == menu_tts_save.inl's historical "phase"
static const int SQO_BLOCK_CUR = 0x4A;
static const int SQO_QUERY_CUR = 0x4F;   // 0 = first option, 1 = second
static const int SQO_SLOT_CUR  = 0x58;

static const uint16_t SQ_STATE_OVERWRITE = 0x37;
static const uint8_t  SQ_PANEL_OVERWRITE = 4;

// The two historical bases, kept as a fallback so a change in allocation order
// degrades to what every other save-screen reader already does. Both are
// validated against the update function before use -- a fallback that can be
// wrong silently is worse than no fallback at all.
static const int SQ_FALLBACK_MODE1 = 0x1A6;   // pool slot 1: +0x1A6 + 0x48 = +0x1EE
static const int SQ_FALLBACK_MODE6 = 0x21E;   // pool slot 2: +0x21E + 0x48 = +0x266

// ---- the generic yes/no query window (0x004C2A20) --------------------------
static const uintptr_t SQ_PROMPT_PTR = 0x01D77300;
static const uintptr_t SQ_OPT_PTR[2] = { 0x01D772F0, 0x01D772E0 };

// The text getter's own "no string" sentinels, as documented in
// menu_tts_magic.inl's help-bar reader.
static const uintptr_t SQ_TEXT_FALLBACK = 0x01D7714C;
static const uintptr_t SQ_TEXT_EMPTY    = 0x01CFF84C;

// Only used when a pointer is null or faults. Never in the normal case.
static const char* SQ_DEFAULT_PROMPT  = "Data exists. Overwrite?";
static const char* SQ_DEFAULT_OPT[2]  = { "Yes", "No" };

// ---------------------------------------------------------------------------
static bool    s_sqOpen        = false;
static uint8_t s_sqLastCursor  = 0xFF;
static bool    s_sqFallbackWarned = false;
static DWORD   s_sqLastPoll    = 0;

static void ResetSaveQueryDialog()
{
    s_sqOpen       = false;
    s_sqLastCursor = 0xFF;
    s_saveQueryDialogOpen = false;
}

// Walk the MRU list for the Save module. Bounded by the pool so a corrupt
// pointer cannot walk off into the process, capped at 12 hops so a cycle cannot
// hang the game thread. Identical shape to FindMagicModule by design: if one of
// them ever needs a fix, the other needs the same one.
static uint8_t* SaveQueryFindModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)SQ_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < SQ_POOL_BASE || a >= SQ_POOL_END) break;
            if ((a - SQ_POOL_BASE) % 0x78 != 0) break;      // not a pool slot
            if (*(uint32_t*)(m + SQO_UPDATE_FN) == SQ_SAVE_STATE_FN) return m;
            m = *(uint8_t* volatile*)m;                     // ->next
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

// The historical addresses, accepted only if the update function agrees.
static uint8_t* SaveQueryModuleFallback()
{
    if (!pMenuStateA) return nullptr;
    __try {
        const int offs[2] = { SQ_FALLBACK_MODE6, SQ_FALLBACK_MODE1 };
        for (int i = 0; i < 2; i++) {
            uint8_t* m = (uint8_t*)pMenuStateA + offs[i];
            if (*(uint32_t*)(m + SQO_UPDATE_FN) == SQ_SAVE_STATE_FN) return m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

// Everything the announcement needs, copied out in one SEH-protected pass.
// POD only: this function must not share a frame with anything that unwinds
// (MSVC C2712 -- tests/lint_seh.py enforces it).
struct SaveQueryRead {
    uint16_t state;
    uint8_t  panel;
    uint8_t  cursor;
    uint8_t  block;
    uint8_t  slot;
    uint8_t  prompt[192];
    int      promptLen;      // -1 = faulted, 0 = no text
    uint8_t  option[2][48];
    int      optionLen[2];
};

// Copy a NUL-terminated text-stream string, translating the line break (0x02)
// to a space. MagicTextToGlyphs drops every control byte, so a two-line prompt
// would otherwise arrive with its words welded together; the overwrite prompt
// is one line today and this costs nothing to be right about tomorrow.
static int SaveQueryCopyText(const uint8_t* p, uint8_t* out, size_t cap)
{
    const uintptr_t a = (uintptr_t)p;
    if (!p || a == SQ_TEXT_FALLBACK || a == SQ_TEXT_EMPTY) return 0;
    size_t n = 0;
    while (n + 1 < cap && p[n] != 0) {
        const uint8_t b = p[n];
        out[n] = (b == 0x02) ? (uint8_t)0x20 : b;
        n++;
    }
    out[n] = 0;
    return (int)n;
}

static bool SaveQueryRawRead(const uint8_t* mod, SaveQueryRead* r)
{
    memset(r, 0, sizeof(*r));
    r->promptLen = -1;
    r->optionLen[0] = r->optionLen[1] = -1;
    __try {
        r->state  = *(uint16_t*)(mod + SQO_STATE);
        r->panel  = *(mod + SQO_PANEL);
        r->cursor = *(mod + SQO_QUERY_CUR);
        r->block  = *(mod + SQO_BLOCK_CUR);
        r->slot   = *(mod + SQO_SLOT_CUR);

        r->promptLen = SaveQueryCopyText(*(const uint8_t* volatile*)SQ_PROMPT_PTR,
                                         r->prompt, sizeof(r->prompt));
        for (int i = 0; i < 2; i++) {
            r->optionLen[i] = SaveQueryCopyText(*(const uint8_t* volatile*)SQ_OPT_PTR[i],
                                                r->option[i], sizeof(r->option[i]));
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Decode one captured string, or fall back to the literal. Kept separate from
// the read above because it returns a std::string.
static std::string SaveQueryDecode(const uint8_t* raw, int len, const char* fallback)
{
    if (len <= 0) return std::string(fallback);
    uint8_t glyphs[192];
    const size_t gn = MagicTextToGlyphs(raw, (size_t)len, glyphs, sizeof(glyphs));
    std::string s = FF8TextDecode::DecodeMenuText(glyphs, gn);

    // The game's own string is "Data exists.  Overwrite?" -- two spaces, which
    // is typesetting, not meaning. Collapse runs and trim so the utterance
    // reads as one sentence.
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (size_t i = 0; i < s.size(); i++) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += c;
    }
    if (out.empty()) return std::string(fallback);
    return out;
}

// The wording, as a pure function of the two decoded strings, so
// tests/menu_save_query_compile.cpp can pin it without a running game.
//
// Aaron's call (2026-08-17): the game's words and the current option, nothing
// else. The block's contents were spoken a keypress ago, when the cursor landed
// on it, and repeating them here would put a paragraph between the question and
// the answer the player is about to give.
static std::string SaveQueryLine(const std::string& prompt, const std::string& option)
{
    std::string s = prompt;
    if (!s.empty() && !option.empty()) s += ' ';
    s += option;
    if (!s.empty() && s[s.size() - 1] != '.' && s[s.size() - 1] != '?' && s[s.size() - 1] != '!')
        s += '.';
    return s;
}

// ---------------------------------------------------------------------------
// The poll. Self-gating on the module walk, so it is safe to call every frame
// from MenuTTS::Update in any game mode -- the save screen is mode 1 from a
// save point and mode 6 from the menu, and this file does not need to know
// which.
static void PollSaveOverwriteDialog()
{
    const DWORD now = GetTickCount();
    if (now - s_sqLastPoll < 60) return;
    s_sqLastPoll = now;

    uint8_t* mod = SaveQueryFindModule();
    if (!mod) {
        mod = SaveQueryModuleFallback();
        if (mod && !s_sqFallbackWarned) {
            s_sqFallbackWarned = true;
            Log::Menu("[SaveQuery] pool walk found no Save module; using the historical "
                      "base 0x%08X (update fn verified)", (unsigned)(uintptr_t)mod);
        }
    }
    if (!mod) { ResetSaveQueryDialog(); return; }

    SaveQueryRead r;
    if (!SaveQueryRawRead(mod, &r)) {
        Log::Menu("[SaveQuery] fault reading module 0x%08X", (unsigned)(uintptr_t)mod);
        ResetSaveQueryDialog();
        return;
    }

    const bool open = (r.state == SQ_STATE_OVERWRITE && r.panel == SQ_PANEL_OVERWRITE);

    if (!open) {
        if (s_sqOpen) {
            // Cursor 0 committed the save and the screen moves on by itself.
            // Anything else came back to the block list, where the game redraws
            // "Choose block to save" and says nothing -- so clear the block
            // latch and let menu_tts_save.inl re-read the block the player is
            // standing on. That reuses a line already in the build rather than
            // inventing a new one.
            const bool cancelled = (s_sqLastCursor != 0);
            Log::Menu("[SaveQuery] overwrite dialog closed (%s), state=0x%02X panel=%u",
                       cancelled ? "cancelled" : "confirmed",
                       (unsigned)r.state, (unsigned)r.panel);
            if (cancelled) s_prevBlockCursor = 0xFF;
            ResetSaveQueryDialog();
        }
        return;
    }

    if (r.cursor > 1) return;   // never seen; do not guess an option name

    const std::string prompt = SaveQueryDecode(r.prompt, r.promptLen, SQ_DEFAULT_PROMPT);
    const std::string option = SaveQueryDecode(r.option[r.cursor], r.optionLen[r.cursor],
                                               SQ_DEFAULT_OPT[r.cursor]);

    if (!s_sqOpen) {
        s_sqOpen = true;
        s_saveQueryDialogOpen = true;
        s_sqLastCursor = r.cursor;
        const std::string line = SaveQueryLine(prompt, option);
        ScreenReader::Speak(line.c_str(), true);
        Log::Menu("[SaveQuery] opened on block %u slot %u, cursor=%u: \"%s\"",
                   (unsigned)r.block + 1, (unsigned)r.slot + 1,
                   (unsigned)r.cursor, line.c_str());
        return;
    }

    if (r.cursor != s_sqLastCursor) {
        s_sqLastCursor = r.cursor;
        ScreenReader::Speak(option.c_str(), true);
        Log::Menu("[SaveQuery] cursor -> %u: \"%s\"", (unsigned)r.cursor, option.c_str());
    }
}
