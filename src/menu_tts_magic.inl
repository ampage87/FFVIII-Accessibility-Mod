// menu_tts_magic.inl -- v0.22.0 (#81)
//
// The Magic submenu, hooked. This file is ONLY responsible for addresses: find
// the module, read the bytes, hand a MagicView to menu_magic_model.inl, and
// speak what comes back. Every decision about wording lives in the model, where
// tests/menu_sim.cpp can drive it offline.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE. Included AFTER menu_magic_model.inl
// and after menu_tts_junction.inl (it reuses the roster and the dream-party
// name rule). Do NOT compile standalone.
//
// ---------------------------------------------------------------------------
// THE STRUCTURAL FACT THIS FILE IS BUILT ON
//
// Everything the mod has called "pMenuStateA + 0x2xx" for the last two years is
// really a field of a MENU MODULE OBJECT. The game allocates modules from a pool
// at 0x01D76BC8 -- stride 0x78, 10 slots, allocator 0x004BE540 -- and threads
// them onto an MRU-first linked list whose head is at 0x01D76B48. Each module
// begins:
//
//     +0x00 next   +0x04 prev   +0x08 update fn   +0x0C draw fn
//     +0x10 state  +0x12 in-use flag              +0x20.. per-module scratch
//
// The main menu happens to land in slot 1 and the open submenu in slot 2, which
// is why `pMenuStateA + 0x22E` has worked: it is slot 2 + 0x10, the module's
// STATE word. (And `+0x230`, which this mod's comments call a "phase", is
// really slot 2's in-use flag -- it was never a phase.)
//
// **That is a coincidence of allocation order, not a guarantee.** So this file
// does not rely on it: it walks the list and matches the Magic module by its
// update function, 0x004F02F0, taken from the submenu dispatch table at
// 0x00B87ED8 index 3. If the walk ever fails it falls back to the historical
// slot-2 address and says so in the log once, so a future breakage is a log
// line rather than a silent misread.
//
// tests/menu_sim.cpp proves the walk finds Magic in all ten pool slots.
// ---------------------------------------------------------------------------

// ---- the module pool -------------------------------------------------------
static const uintptr_t MM_POOL_BASE   = 0x01D76BC8;
static const uintptr_t MM_POOL_END    = 0x01D77078;   // base + 10 * 0x78
static const uintptr_t MM_LIST_HEAD   = 0x01D76B48;
static const uint32_t  MM_MAGIC_STATE_FN = 0x004F02F0;  // dispatch table 0x00B87ED8 index 3

// ---- module field offsets (see menu_magic_model.inl's header for provenance)
static const int MMO_STATE        = 0x10;
static const int MMO_DIALOG_CHAR  = 0x32;
static const int MMO_DIALOG_SLOT  = 0x33;
static const int MMO_TARGET_MASK  = 0x36;
static const int MMO_CURSOR_BASE  = 0x38;   // + charId
static const int MMO_PAGE         = 0x42;
static const int MMO_SCREEN_MODE  = 0x56;
static const int MMO_TARGET_CUR   = 0x57;
static const int MMO_TARGET_COUNT = 0x60;
static const int MMO_ACTION_CUR   = 0x61;
static const int MMO_SECOND_CHAR  = 0x62;
static const int MMO_CHAR_ID      = 0x64;
static const int MMO_ACTION_MASK  = 0x67;
static const int MMO_DIALOG_OPEN  = 0x6E;
static const int MMO_DIALOG_CUR   = 0x70;
static const int MMO_SORT_CUR     = 0x71;
// v0.22.1: the Exchange flow, plus the help-bar string pointer.
static const int MMO_HELP_PTR     = 0x24;   // -> NUL-terminated FF8 bytes in mngrp.bin
static const int MMO_PAGE_B       = 0x46;   // partner's page
static const int MMO_SPLIT_TAKE   = 0x58;
static const int MMO_SPLIT_LEAVE  = 0x59;
static const int MMO_SPLIT_SPELL  = 0x5A;
static const int MMO_POPUP_KIND   = 0x5E;   // bit0 = 2-entry, bit1 = 3-entry
static const int MMO_POPUP_CUR    = 0x5F;

// ---- game data -------------------------------------------------------------
static const uintptr_t MM_SAVEMAP        = 0x01CFDC5C;
static const int       MM_CHAR_ARRAY_OFF = 0x048C;   // savemap -> character[0]
static const int       MM_CHAR_STRIDE    = 0x98;     // 152 bytes
static const int       MM_CHAR_MAGIC_OFF = 0x10;     // Magics[32], {id, qty}
static const int       MM_CHAR_MODEL_OFF = 0x08;     // model id (dream-party rule)
static const int       MM_MENU_LOCK_OFF  = 0x0AE3;
static const uintptr_t MM_MMAGIC_PTR     = 0x01D2BB10;  // -> mmagic.bin
static const uintptr_t MM_MAGIC_DATA     = 0x01CF4064;  // stride 60, +0x07 = target type

// ---------------------------------------------------------------------------
static uint8_t* s_mmModule       = nullptr;
static bool     s_mmActive       = false;
static bool     s_mmFallbackWarned = false;
static MagicPhase s_mmPhase      = MP_NONE;
static char     s_mmLastSpoken[256] = {0};
// v0.22.3: whose magic the last announcement was about. A character change does
// not change the phase, so without this the header never fires and nothing says
// you are now looking at somebody else's list.
static int      s_mmLastChar     = -1;
// v0.22.4: the All transfer runs in state 105, which lasts ONE FRAME. The
// 2026-08-16 log proves the poll cannot catch it -- Irvine's whole list moved to
// Zell and the mod said nothing. So watch the EFFECT instead: remember how much
// the giver was holding while the giver step is up, and announce when it hits
// zero. Cancel leaves the total untouched, so the two are distinguishable
// without ever seeing the state that did the work.
static int      s_mmAllGiver     = -1;
static int      s_mmAllReceiver  = -1;
static long     s_mmAllGiverHeld = 0;

static void ResetMagicSubmenu()
{
    s_mmModule = nullptr;
    s_mmActive = false;
    s_mmPhase  = MP_NONE;
    s_mmLastSpoken[0] = '\0';
    s_mmLastChar = -1;
    s_mmAllGiver = s_mmAllReceiver = -1;
    s_mmAllGiverHeld = 0;
}

// Walk the MRU list for the module whose update function is the Magic state
// machine. Bounded by the pool so a corrupt pointer cannot walk off into the
// process; capped at 12 hops so a cycle cannot hang the game thread.
static uint8_t* FindMagicModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)MM_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < MM_POOL_BASE || a >= MM_POOL_END) break;
            if ((a - MM_POOL_BASE) % 0x78 != 0) break;          // not a pool slot
            if (*(uint32_t*)(m + 0x08) == MM_MAGIC_STATE_FN) return m;
            m = *(uint8_t* volatile*)m;                          // ->next
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

// The historical address, kept as a fallback so a change in the pool layout
// degrades to "as good as every other submenu" rather than to silence.
static uint8_t* MagicModuleFallback()
{
    if (!pMenuStateA) return nullptr;
    return (uint8_t*)pMenuStateA + 0x21E;
}

static const char* MagicCharName(uint8_t charIdx)
{
    static const char* NAMES[] = {
        "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea",
        "Laguna", "Kiros", "Ward"
    };
    if (charIdx > 10) return "Unknown";
    // Same dream-party rule as Junction (v0.17.8.17.7): during a Laguna dream
    // the roster index is stale but the loaded character's model id reads 8/9/10.
    uint8_t modelId = charIdx;
    __try {
        modelId = *(uint8_t*)(MM_SAVEMAP + MM_CHAR_ARRAY_OFF + charIdx * MM_CHAR_STRIDE + MM_CHAR_MODEL_OFF);
    } __except(EXCEPTION_EXECUTE_HANDLER) { modelId = charIdx; }
    if (modelId >= 8 && modelId <= 10) return NAMES[modelId];
    return NAMES[charIdx];
}

// Fill the view. Returns false if anything could not be read -- the caller then
// says nothing at all, which is the right failure for a screen reader: silence
// is recoverable, a confidently wrong spell name is not.
static bool FillMagicView(uint8_t* mod, MagicView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state        = *(uint16_t*)(mod + MMO_STATE);
        v.screenMode   = *(mod + MMO_SCREEN_MODE);
        v.charId       = *(mod + MMO_CHAR_ID);
        v.page         = *(mod + MMO_PAGE);
        v.actionCursor = *(mod + MMO_ACTION_CUR);
        v.actionMask   = *(mod + MMO_ACTION_MASK);
        v.targetCursor = *(mod + MMO_TARGET_CUR);
        v.targetCount  = *(mod + MMO_TARGET_COUNT);
        v.targetMask   = *(uint16_t*)(mod + MMO_TARGET_MASK);
        v.sortCursor   = *(mod + MMO_SORT_CUR);
        v.dialogOpen   = *(uint16_t*)(mod + MMO_DIALOG_OPEN);
        v.dialogCursor = *(mod + MMO_DIALOG_CUR);
        v.dialogChar   = *(mod + MMO_DIALOG_CHAR);
        v.dialogSlot   = *(mod + MMO_DIALOG_SLOT);
        v.secondChar   = *(mod + MMO_SECOND_CHAR);
        if (v.charId > 7) return false;
        v.cursorRaw    = *(mod + MMO_CURSOR_BASE + v.charId);

        // v0.22.1: the Exchange screen's second column and its popups. Both
        // columns share the one per-character cursor array, so the partner's
        // cursor is the same array indexed by the partner's id.
        v.pageB        = *(mod + MMO_PAGE_B);
        v.popupCursor  = *(mod + MMO_POPUP_CUR);
        v.popupKind    = *(mod + MMO_POPUP_KIND);
        v.splitTake    = *(mod + MMO_SPLIT_TAKE);
        v.splitLeave   = *(mod + MMO_SPLIT_LEAVE);
        v.splitSpell   = *(mod + MMO_SPLIT_SPELL);
        v.cursorRawB   = (v.secondChar <= 7) ? *(mod + MMO_CURSOR_BASE + v.secondChar) : 0;

        // The character's 32 magic slots, straight out of the savemap. The
        // menu does NOT remap the list for display -- cursor index is slot
        // index -- which the draw path at 0x004F05B7 confirms.
        const uint8_t* mg = (const uint8_t*)(MM_SAVEMAP + MM_CHAR_ARRAY_OFF
                                             + v.charId * MM_CHAR_STRIDE + MM_CHAR_MAGIC_OFF);
        for (int i = 0; i < 32; i++) {
            v.slots[i].id  = mg[i * 2];
            v.slots[i].qty = mg[i * 2 + 1];
        }
        if (v.secondChar <= 7) {
            const uint8_t* mb = (const uint8_t*)(MM_SAVEMAP + MM_CHAR_ARRAY_OFF
                                                 + v.secondChar * MM_CHAR_STRIDE + MM_CHAR_MAGIC_OFF);
            for (int i = 0; i < 32; i++) {
                v.slotsB[i].id  = mb[i * 2];
                v.slotsB[i].qty = mb[i * 2 + 1];
            }
        }

        v.menuLock = *(uint8_t*)(MM_SAVEMAP + MM_MENU_LOCK_OFF);

        // mmagic.bin is loaded at runtime; the pointer can legitimately be null
        // before the menu module has ever opened. Treat that as "nothing is
        // castable" rather than reading through it.
        const uint8_t* mmagic = *(const uint8_t* volatile*)MM_MMAGIC_PTR;
        for (int id = 0; id < MAGIC_SPELL_NAME_COUNT; id++) {
            v.mmagicFlag[id] = mmagic ? mmagic[id * 4] : 0;
            v.targetType[id] = *(uint8_t*)(MM_MAGIC_DATA + id * 60 + 0x07);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    v.charName = MagicCharName(v.charId);
    for (int i = 0; i < 8; i++) v.memberName[i] = MagicCharName((uint8_t)i);
    return true;
}

// ---------------------------------------------------------------------------
// The poll. Called once per menu tick while the top-level cursor is on Magic.
//
// Change detection is on the COMPOSED SENTENCE rather than on individual
// cursor bytes. That is deliberate: the list's position suffix ("slot 6 of 32")
// makes every line unique, so paging, character changes and cursor moves all
// fall out of one comparison, and a sort that reorders the whole list
// re-announces because the name under the cursor changed. Tracking six cursors
// separately is what makes these files long and wrong.
// ---------------------------------------------------------------------------
static void PollMagicSubmenu()
{
    if (!pMenuStateA) return;

    // Gate: top-level cursor on Magic (2) and the Magic subsystem active (3).
    uint8_t sub = 0xFF;
    __try { sub = *((uint8_t*)pMenuStateA + 0x1E8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (s_prevCursor != 2 || sub != 3) {
        if (s_mmActive) {
            Log::Menu("[MagicTTS] leaving the Magic submenu");
            ResetMagicSubmenu();
        }
        return;
    }

    uint8_t* mod = FindMagicModule();
    if (!mod) {
        mod = MagicModuleFallback();
        if (mod && !s_mmFallbackWarned) {
            s_mmFallbackWarned = true;
            Log::Menu("[MagicTTS] module walk found no Magic module (update fn 0x%08X not in "
                      "the pool list) -- falling back to pMenuStateA+0x21E. If the spell "
                      "names are wrong, THIS is why.", MM_MAGIC_STATE_FN);
        }
        if (!mod) return;
    } else if (!s_mmActive) {
        uint8_t* expect = MagicModuleFallback();
        Log::Menu("[MagicTTS] Magic module at 0x%08X (pool slot %d)%s",
                  (unsigned)(uintptr_t)mod,
                  (int)(((uintptr_t)mod - MM_POOL_BASE) / 0x78),
                  (mod == expect) ? " -- the historical slot-2 address"
                                  : " -- NOT slot 2; the walk earned its keep");
    }
    s_mmModule = mod;

    MagicView v;
    if (!FillMagicView(mod, v)) return;

    const MagicPhase phase = MagicPhaseOf(v);

    // --- the All transfer, detected by its effect -------------------------
    if (phase == MP_ALL_GIVER) {
        // Re-snapshot every tick: the giver can be changed with left/right.
        const long held = MagicTotalHeld(v.slotsB);
        if (s_mmAllGiver != (int)v.secondChar || held > s_mmAllGiverHeld) {
            s_mmAllGiver     = (int)v.secondChar;
            s_mmAllReceiver  = (int)v.charId;
            s_mmAllGiverHeld = held;
        }
    } else if (s_mmAllGiver >= 0) {
        // **STAY ARMED THROUGH THE TRANSIENTS.** v0.22.4 cleared the latch on
        // any poll that was not state 97, and the chain from the giver step to
        // the transfer is 99 -> 104 -> 105 -> 96 -> 97 -- so a poll landing on
        // 104 or 96 (both MP_NONE) disarmed it a frame or two BEFORE the
        // transfer it existed to detect. That is why the 2026-08-16 run was
        // still silent. The latch now survives until the player is demonstrably
        // out of the flow: back on the action row, closing, or a different
        // giver. Nothing else empties a character's whole list in place, and
        // reaching Exchange to do it by hand requires passing through the
        // action row, which clears this first.
        const bool sameGiver = ((int)v.secondChar == s_mmAllGiver);
        const long nowHeld   = sameGiver ? MagicTotalHeld(v.slotsB) : -1;
        if (sameGiver && s_mmAllGiverHeld > 0 && nowHeld == 0) {
            char done[256];
            const char* from = (s_mmAllGiver    < 8) ? MagicCharName((uint8_t)s_mmAllGiver)    : "Member";
            const char* to   = (s_mmAllReceiver < 8) ? MagicCharName((uint8_t)s_mmAllReceiver) : "Member";
            snprintf(done, sizeof(done), "All magic moved from %s to %s", from, to);
            ScreenReader::Speak(done, true);
            Log::Menu("[MagicTTS] All transfer detected: %s held %ld, now 0 -> \"%s\"",
                      from, s_mmAllGiverHeld, done);
            s_mmLastSpoken[0] = '\0';   // let the next screen re-announce
            s_mmAllGiver = s_mmAllReceiver = -1;
            s_mmAllGiverHeld = 0;
        } else if (!sameGiver || phase == MP_ACTION || phase == MP_CLOSING) {
            s_mmAllGiver = s_mmAllReceiver = -1;
            s_mmAllGiverHeld = 0;
        }
    }

    if (!s_mmActive) {
        s_mmActive = true;
        s_mmPhase = MP_NONE;
        s_mmLastSpoken[0] = '\0';
        s_mmLastChar = -1;
    }

    if (phase == MP_NONE || phase == MP_CLOSING) {
        // Paging and slides: stay quiet rather than read a half-built frame.
        //
        // v0.22.1: **and do NOT record the transient phase.** v0.22.0 wrote it
        // to s_mmPhase, so a page turn went 13 -> 14 -> 13 and the return looked
        // like a phase change, which re-spoke the "Magic list" header on every
        // single page. Keeping the last SPOKEN phase means the header is said
        // once, when focus actually lands in the list.
        return;
    }

    char line[256];
    MagicAnnounce(v, phase, line, sizeof(line));
    if (line[0] == '\0') { s_mmPhase = phase; return; }
    // v0.22.1: these two are one-shot events, not cursor positions -- say them
    // every time they occur even though the sentence is unchanged.
    if (phase == MP_ALL_DONE || phase == MP_ALL_WARN) s_mmLastSpoken[0] = '\0';

    // v0.22.3: **a character change counts as an arrival.** L1/R1 swaps the
    // character without leaving the phase, so the 2026-08-16 log shows the spell
    // list going from Irvine's Fire to Selphie's Cure with nothing said about
    // whose magic it now is -- and on the action row, Zell to Selphie announced
    // only as the position qualifier quietly disappearing. Which character the
    // announcement is ABOUT differs by phase: the partner panel is about the
    // partner, everything else about the screen's own character.
    const int aboutChar = (phase == MP_XCHG_THEIRS || phase == MP_XCHG_PARTNER)
                        ? (int)v.secondChar : (int)v.charId;
    // v0.22.5: ...but only where the LINE does not already name the character.
    // On the All steps the line is "Rinoa, receives", so forcing the header
    // there prefixed "Select member to receive magic" to every left/right.
    const bool charChanged = (s_mmLastChar >= 0 && aboutChar != s_mmLastChar
                              && !MagicLineNamesCharacter(phase));
    const bool phaseChanged = (phase != s_mmPhase) || charChanged;
    if (!phaseChanged && strcmp(line, s_mmLastSpoken) == 0) return;

    if (phaseChanged) {
        char hdrBuf[96];
        MagicPhaseHeaderBuf(v, phase, hdrBuf, sizeof(hdrBuf));
        const char* hdr = hdrBuf;
        if (hdr[0]) {
            char full[416];   // header (96) + line (256) + separator, no truncation
            snprintf(full, sizeof(full), "%s. %s", hdr, line);
            ScreenReader::Speak(full, true);
        } else {
            ScreenReader::Speak(line, true);
        }
    } else {
        ScreenReader::Speak(line, true);
    }

    snprintf(s_mmLastSpoken, sizeof(s_mmLastSpoken), "%s", line);
    s_mmPhase = phase;
    s_mmLastChar = aboutChar;

    Log::Menu("[MagicTTS] phase=%d state=%u mode=%u char=%u/%u page=%u/%u cur=%u -> slot %d/%d : \"%s\"",
              (int)phase, (unsigned)v.state, (unsigned)v.screenMode,
              (unsigned)v.charId, (unsigned)v.secondChar,
              (unsigned)v.page, (unsigned)v.pageB, (unsigned)v.cursorRaw,
              MagicSlotIndex(v), MagicSlotIndexB(v), line);
}

// ---------------------------------------------------------------------------
// v0.22.1: THE HELP BAR.
//
// Aaron pressed "/" on the Magic screen and nothing was read. The mod's usual
// AnnounceHelpText scrapes the RENDERED text out of the GCW buffer and looks for
// a dash separator or a known prefix; the Magic bar has neither, so the key was
// silently a no-op there.
//
// It never needed the scrape. The Magic module caches a pointer to the exact
// string the bar is drawing:
//
//     0x004F6996: push 0x4f70c0            ; the bar's content callback
//     0x004F699B: lea  edx, [esi + 0x24]   ; ctx = &module[+0x24]
//     0x004F70D0: mov  eax, [ecx + eax*4]  ; +0x24 current, +0x28 outgoing
//
// and the text getter 0x004BD630 does no copying -- it returns a pointer
// straight into the loaded mngrp.bin image. So +0x24 is raw, NUL-terminated,
// FF8-encoded bytes in long-lived data, written with one aligned dword store.
// That makes it safe to read from the mod's thread, which a per-frame scratch
// buffer would not be.
//
// Two sentinels mean "no text" (0x01D7714C is the getter's own fallback and
// 0x01CFF84C is the empty-string constant); NULL means the bar is blank.
//
// Known limitation, worth stating rather than hiding: the game does not rewrite
// +0x24 in target select (state 20) or in All's step 2 (state 99). The string
// left there is still the correct one for those screens -- it just was not
// refreshed -- so this reads slightly stale text rather than wrong text.
// ---------------------------------------------------------------------------
// The raw copy lives in its own function ON PURPOSE. MSVC rejects a function
// that mixes __try/__except with anything needing object unwinding (C2712), and
// the decode below returns a std::string -- so the SEH-protected read cannot
// share a frame with it. tests/lint_seh.py catches this exact mistake; it caught
// it here, before the build did.
//
// Returns the byte count, 0 for "no text", or -1 on a fault.
static int MagicHelpRawCopy(const uint8_t* mod, uint8_t* out, size_t cap, uintptr_t* addrOut)
{
    *addrOut = 0;
    __try {
        const uint8_t* txt = *(const uint8_t* volatile*)(mod + MMO_HELP_PTR);
        const uintptr_t a = (uintptr_t)txt;
        *addrOut = a;
        // Two sentinels mean "no text": 0x01D7714C is the text getter's own
        // fallback and 0x01CFF84C is the empty-string constant. NULL means the
        // bar is simply blank, which is what an empty magic slot produces.
        if (!txt || a == 0x01D7714C || a == 0x01CFF84C) return 0;
        size_t len = 0;
        while (len < cap && txt[len] != 0) { out[len] = txt[len]; len++; }
        return (int)len;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static bool AnnounceMagicHelpText()
{
    uint8_t* mod = FindMagicModule();
    if (!mod) return false;   // not on the Magic screen: let the GCW path run

    uint8_t   raw[256];
    uintptr_t addr = 0;
    const int len = MagicHelpRawCopy(mod, raw, sizeof(raw), &addr);

    if (len < 0) {
        Log::Menu("[MagicTTS] help bar: fault reading 0x%08X", (unsigned)addr);
        ScreenReader::Speak("No help text", true);
        return true;
    }
    if (len == 0) {
        ScreenReader::Speak("No help text", true);
        Log::Menu("[MagicTTS] help bar: pointer 0x%08X carries no text", (unsigned)addr);
        return true;
    }

    // v0.22.2: **convert before decoding.** DecodeMenuText indexes its table
    // with a GLYPH INDEX -- its existing caller feeds it the GCW buffer, which
    // the renderer has already converted. mngrp.bin is one stage earlier and
    // holds TEXT-STREAM bytes, which are `glyph + 0x20`. Passing them straight
    // in shifted every character 32 slots and read "Use magic" aloud as
    // "AaI'UEIOE". DecodeMenuText itself is left alone; its other callers are
    // correct and would break if it moved.
    uint8_t glyphs[256];
    const size_t gn = MagicTextToGlyphs(raw, (size_t)len, glyphs, sizeof(glyphs));
    std::string s = FF8TextDecode::DecodeMenuText(glyphs, gn);
    if (s.empty()) {
        ScreenReader::Speak("No help text", true);
        Log::Menu("[MagicTTS] help bar: %d bytes decoded to nothing", len);
        return true;
    }
    ScreenReader::Speak(s.c_str(), true);
    Log::Menu("[MagicTTS] help bar @0x%08X (%d bytes): \"%s\"", (unsigned)addr, len, s.c_str());
    return true;
}
