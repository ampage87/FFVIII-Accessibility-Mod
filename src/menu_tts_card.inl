// menu_tts_card.inl -- v0.24.0 (#83)
//
// The Card album: the main menu's "Card" entry, which the mod had never spoken
// at all. PART OF menu_tts.cpp -- TEXTUAL INCLUDE, after menu_card_model.inl.
// Do NOT compile standalone.
//
// This file is ONLY responsible for addresses. Every decision about wording is
// in menu_card_model.inl, driven offline by tests/menu_sim.cpp.
//
// ---------------------------------------------------------------------------
// ADDRESSES -- all from docs/CARD_MENU_FINDINGS.md
//
// Module: dispatch index 7 (pMenuStateA + 0x1E8 == 7), creator 0x004EF020,
// update 0x004EF6F0, draw 0x004EF750, jump table 0x004EF6BC, 13 states.
//
// **The module is found by WALKING THE POOL for update == 0x004EF6F0**, not by
// assuming pool slot 2. The Magic work established that slot 2 is an
// allocation-order coincidence, and the card album is reached from the main menu
// by a different path than the screens that happened to land there.
//
// **State 5 is the only steady state.** States 7 and 9 are the page-slide
// animations -- and they DO read the input word, because they queue a further
// left/right flip mid-slide. That is worth saying out loud: on the Junction
// screen the test "does this state read input" would have been enough to find
// the grid, and here it is not. The test that works is "can the player sit
// still in it": 7 and 9 auto-advance to 5 when the slide finishes.
// ---------------------------------------------------------------------------

static const uint32_t CD_UPDATE_FN = 0x004EF6F0;   // the Card album state machine

// Module field offsets.
static const int CDO_STATE   = 0x10;   // u16
static const int CDO_CURSOR  = 0x2E;   // u16, 0..109 -- the card id itself
static const int CDO_TOT_MON = 0x32;   // u16 category subtotals the game itself computes
static const int CDO_TOT_BOS = 0x34;
static const int CDO_TOT_GF  = 0x36;
static const int CDO_TOT_PLR = 0x38;
static const int CDO_TOT_ALL = 0x3A;

// Savemap. The two encodings are the game's, not a simplification:
//   ids 0..76   byte = 0x80 | count      (0x00 = never seen)
//   ids 77..109 byte = owner code        (0xF0 = you hold it, 0x00 = used up,
//                                         anything else = an NPC has it)
//               and "have you ever seen it" is bit (id-77) of the bitfield.
// Reproduced from 0x00534950 rather than called, because calling into the game
// from the mod's thread is a risk this screen does not need to take.
static const uintptr_t CD_COUNTS   = 0x01CFEF38;   // 110 bytes
static const uintptr_t CD_RARE_BITS = 0x01CFEFA6;  // 5 bytes, 33 bits, ids 77..109
static const int       CD_RARE_FIRST = 77;

// The "AREA" line under a RARE card: who is holding it right now. Static for the
// common cards (generated into CARD_SOURCES) but dynamic for these, so it is
// resolved the way the game resolves it at 0x004EFE30 / 0x004C0660:
//
//     textId = byte[0x00B96878 + ownerByte]
//     bank   = [0x01D2BB48]                    ; the loaded areames strings
//     if (textId >= u16[bank]) textId = 0
//     str    = bank + u16[bank + textId*2 + 2]
//
// Two reads and a bounds check -- no file parsing, and no calling into the game
// from the mod's thread.
static const uintptr_t CD_OWNER_TEXTID = 0x00B96878;   // owner byte -> areames text id
static const uintptr_t CD_AREAMES_PTR  = 0x01D2BB48;   // -> {u16 count, u16 offsets[], text}

// ---------------------------------------------------------------------------
static bool s_cdActive = false;
static int  s_cdState  = -1;
static int  s_cdLevel  = -1;
static char s_cdLastSpoken[256] = {0};

static void ResetCardMenu()
{
    s_cdActive = false;
    s_cdState  = -1;
    s_cdLevel  = -1;
    s_cdLastSpoken[0] = '\0';
}

static uint8_t* FindCardModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)MM_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < MM_POOL_BASE || a >= MM_POOL_END) break;
            if ((a - MM_POOL_BASE) % 0x78 != 0) break;
            if (*(uint32_t*)(m + 0x08) == CD_UPDATE_FN) return m;
            m = *(uint8_t* volatile*)m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

// getCardCount, reproduced from 0x00534950. Returns -1 unknown, 0 seen-not-held,
// >0 held.
static int CardCountOf(int id)
{
    if (id < 0 || id >= CARD_COUNT) return CARD_UNKNOWN;
    __try {
        const uint8_t b = *((const uint8_t*)CD_COUNTS + id);
        if (id < CD_RARE_FIRST) {
            if (!(b & 0x80)) return CARD_UNKNOWN;
            return (int)(b & 0x7F);
        }
        const int bit = id - CD_RARE_FIRST;
        const uint8_t known = *((const uint8_t*)CD_RARE_BITS + (bit >> 3));
        if (!(known & (1u << (bit & 7)))) return CARD_UNKNOWN;
        return (b == 0xF0) ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return CARD_UNKNOWN; }
}

// The raw savemap byte, which for a rare card is an owner code rather than a
// count. Returns -1 on a bad read so callers can stay quiet.
static int CardOwnerByte(int id)
{
    if (id < CD_RARE_FIRST || id >= CARD_COUNT) return -1;
    __try { return (int)*((const uint8_t*)CD_COUNTS + id); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int CardAreaRawCopy(int ownerByte, uint8_t* out, size_t cap)
{
    __try {
        const uint8_t  textId = *((const uint8_t*)CD_OWNER_TEXTID + ownerByte);
        const uint8_t* bank   = *(const uint8_t* volatile*)CD_AREAMES_PTR;
        if (!bank) return 0;
        const uint16_t n = *(const uint16_t*)bank;
        const uint16_t id = (textId >= n) ? 0 : textId;
        const uint16_t off = *(const uint16_t*)(bank + 2 + id * 2);
        const uint8_t* txt = bank + off;
        size_t len = 0;
        while (len < cap && txt[len] != 0) { out[len] = txt[len]; len++; }
        return (int)len;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// SEH and std::string cannot share a frame under MSVC (C2712), hence the split.
static bool CardAreaText(int id, std::string& out)
{
    const int owner = CardOwnerByte(id);
    if (owner < 0) return false;
    if (owner == 0xF0) { out = "you have it";  return true; }
    if (owner == 0x00) { out = "used up";      return true; }
    uint8_t raw[128];
    const int len = CardAreaRawCopy(owner, raw, sizeof(raw));
    if (len <= 0) return false;
    uint8_t glyphs[128];
    const size_t gn = MagicTextToGlyphs(raw, (size_t)len, glyphs, sizeof(glyphs));
    if (gn == 0) return false;
    out = FF8TextDecode::DecodeMenuText(glyphs, gn);
    return !out.empty();
}

static bool FillCardView(uint8_t* mod, CardView& v)
{
    memset(&v, 0, sizeof(v));
    v.count = CARD_UNKNOWN;
    __try {
        v.state  = *(uint16_t*)(mod + CDO_STATE);
        v.cursor = (int)*(uint16_t*)(mod + CDO_CURSOR);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }

    v.count = CardCountOf(v.cursor);

    // The game's own summary panel, which is on screen the whole time and which
    // the mod was reading none of: five running totals it computes itself at
    // album open. These count cards HELD, duplicates included.
    __try {
        v.gameMonster = *(uint16_t*)(mod + CDO_TOT_MON);
        v.gameBoss    = *(uint16_t*)(mod + CDO_TOT_BOS);
        v.gameGF      = *(uint16_t*)(mod + CDO_TOT_GF);
        v.gamePlayer  = *(uint16_t*)(mod + CDO_TOT_PLR);
        v.gameTotal   = *(uint16_t*)(mod + CDO_TOT_ALL);
    } __except(EXCEPTION_EXECUTE_HANDLER) { }

    // The collection totals. The game keeps its own subtotals at +0x32..+0x3A,
    // but those count DUPLICATES only -- "how many cards do I own" -- and say
    // nothing about coverage, which is the number a collector is actually
    // chasing. Walking 110 bytes once per announcement is free.
    for (int i = 0; i < CARD_COUNT; i++) {
        const int n = CardCountOf(i);
        if (n == CARD_UNKNOWN) continue;
        v.seen++;
        if (n > 0) { v.uniqueHeld++; v.totalHeld += n; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// The poll.
//
// One steady state, one line, and a header whenever the page changes -- the
// level is the only thing that locates you in a hundred and ten cards, and it
// changes with left/right without the line ever mentioning it.
// ---------------------------------------------------------------------------
static void PollCardMenu()
{
    if (!pMenuStateA) return;

    uint8_t sub = 0xFF;
    __try { sub = *((uint8_t*)pMenuStateA + JUNC_ACTIVE_OFFSET); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (sub != CARD_SUBSYSTEM_ID) { if (s_cdActive) ResetCardMenu(); return; }

    uint8_t* mod = FindCardModule();
    if (!mod) { if (s_cdActive) ResetCardMenu(); return; }
    if (!s_cdActive) {
        s_cdActive = true;
        s_cdState = -1; s_cdLevel = -1; s_cdLastSpoken[0] = '\0';
        Log::Menu("[CardTTS] album module at 0x%08X (pool slot %d)",
                  (unsigned)(uintptr_t)mod, (int)(((uintptr_t)mod - MM_POOL_BASE) / 0x78));
    }

    CardView v;
    if (!FillCardView(mod, v)) return;
    if (v.state != CARD_STATE_LIST) return;   // slides and fades say nothing

    char line[256];
    CardAnnounceLine(v, line, sizeof(line));
    if (line[0] == '\0') return;

    const int level = CardLevel(v.cursor);
    const bool arrived    = (s_cdState != CARD_STATE_LIST);
    const bool pageTurned = (s_cdLevel >= 0 && level != s_cdLevel);
    if (!arrived && !pageTurned && strcmp(line, s_cdLastSpoken) == 0) return;

    if (arrived || pageTurned) {
        char hdr[64], full[384];
        CardAnnounceHeader(v, hdr, sizeof(hdr));
        snprintf(full, sizeof(full), "%s. %s", hdr, line);
        ScreenReader::Speak(full, true);
    } else {
        ScreenReader::Speak(line, true);
    }
    snprintf(s_cdLastSpoken, sizeof(s_cdLastSpoken), "%s", line);
    s_cdState = CARD_STATE_LIST;
    s_cdLevel = level;

    Log::Menu("[CardTTS] cursor=%d level=%d count=%d : \"%s\"",
              v.cursor, level, v.count, line);
}

// ---------------------------------------------------------------------------
// Number keys, the same shape as the Status and Junction screens.
//
//   0  the collection: held, different, seen
//   1  where am I: card N of 11, level N of 10, category
//   2  the card under the cursor, in full, with the numbers LABELLED
//
// Key 2 is the deliberate counterpart to the terse list line: four bare numbers
// are right when you are moving, and wrong when you have stopped and want to be
// sure which one was the left.
// ---------------------------------------------------------------------------
// The bottom info line. Kept out of the model because half of it is a live
// string read out of the loaded areames bank.
static void CardSpeakSource(const CardView& v)
{
    const int id = v.cursor;
    if (id < 0 || id >= CARD_COUNT) return;
    if (v.count == CARD_UNKNOWN) { ScreenReader::Speak("Not seen yet", true); return; }

    char buf[256];
    if (id < CD_RARE_FIRST) {
        const char* src = CARD_SOURCES[id];
        if (!src || !src[0]) return;
        snprintf(buf, sizeof(buf), "Carried by %s", src);
    } else {
        std::string area;
        if (!CardAreaText(id, area)) return;
        snprintf(buf, sizeof(buf), "Area, %s", area.c_str());
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[CardTTS] source cursor=%d: %s", id, buf);
}

static void CardNumberKeys()
{
    if (!s_cdActive) return;

    int key = -1;
    for (int k = 0; k <= 3; k++)
        if (GetAsyncKeyState('0' + k) & 1) { key = k; break; }
    if (key < 0) return;

    uint8_t* mod = FindCardModule();
    if (!mod) return;
    CardView v;
    if (!FillCardView(mod, v)) return;

    // Key 3 is the bottom info line the screen always shows under the card:
    // "MONSTER" (which monster carries it) for the 77 common cards, "AREA" (who
    // is holding it) for the 33 rares. It is on a key rather than in the list
    // line because it is a sentence, and the list line is meant to be four
    // numbers you can move through.
    if (key == 3) {
        CardSpeakSource(v);
        return;
    }

    char buf[384];
    buf[0] = '\0';
    if      (key == 0) CardAnnounceTotals(v, buf, sizeof(buf));
    else if (key == 1) CardAnnouncePosition(v, buf, sizeof(buf));
    else               CardAnnounceDetail(v, buf, sizeof(buf));

    if (buf[0] == '\0') return;
    ScreenReader::Speak(buf, true);
    Log::Menu("[CardTTS] key=%d cursor=%d: %s", key, v.cursor, buf);
}
