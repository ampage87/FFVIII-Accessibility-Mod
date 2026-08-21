// menu_tts_tutorial.inl -- v0.26.0 (#85)
//
// The Tutorial menu and the SeeD written exam: the MEMORY-FACING half. PART OF
// menu_tts.cpp -- TEXTUAL INCLUDE, after menu_tutorial_model.inl. Do NOT
// compile standalone.
//
// Addresses only; all wording is in the model.
//
// ---------------------------------------------------------------------------
// TWO MODULES.
//
// Tutorial: dispatch 20, creator 0x004C9B70, update 0x004C9CB0, draw 0x004CAE10.
// Exam:     dispatch 23, creator 0x004D4960, update 0x004D4D30, draw 0x004D58A0.
//
// Choosing Test or Review does not move the Tutorial module out of the way -- it
// pushes the exam on top (Tutorial state 17 calls 0x004BDB30) and then parks in
// its own state 18 polling for the pop. **Both are in the pool at once**, so the
// poll walks for each and lets the exam win. Gating on the active-submenu byte
// alone would have been wrong here in a way it was not on any earlier screen.
// ---------------------------------------------------------------------------

static const uint32_t TUT_UPDATE_FN  = 0x004C9CB0;
static const uint32_t SEED_UPDATE_FN = 0x004D4D30;
// v0.27.0 (#86): the magazine page viewer shared by Battle Operation, Card Game
// Rules and Icon Explanation. One update fn for all three; the record range is
// what distinguishes them.
static const uint32_t MAG_UPDATE_FN  = 0x004C9060;
// v0.28.0 (#87): Information -- the nested page browser.
static const uint32_t TIPS_UPDATE_FN = 0x004D5F10;

// Tutorial module fields.
static const int TUTO_STATE     = 0x10;   // u16
static const int TUTO_DESCPTR   = 0x24;   // pointer to the highlighted row's description
static const int TUTO_TESTPICK  = 0x32;   // s8, flat index over 10-row pages
static const int TUTO_CURSOR    = 0x34;   // s8, row 0..6
static const int TUTO_HELPCUR   = 0x35;   // u8, Online Help cursor
static const int TUTO_HELPCNT   = 0x36;   // u8, how many Help rows survive the flag filter
static const int TUTO_HELPMAP   = 0x39;   // u8[9], row -> descriptor index

// Magazine viewer fields and data.
static const int MAGO_STATE  = 0x10;   // u16
static const int MAGO_RECORD = 0x28;   // dword, the record ON SCREEN (not +0x2C,
                                       // which is the one being moved to)
static const uintptr_t MAG_FIRST_REC = 0x01D7D3A5;   // u8, set by the Tutorial
static const uintptr_t MAG_LAST_REC  = 0x01D7D3A6;   // u8
static const uintptr_t MAG_RECS_PTR  = 0x01D2BAF8;   // -> mmag.bin, 69 x 68 bytes
static const int       MAG_REC_SIZE  = 68;
static const int       MAG_TEXTBLK   = 0x34;         // 4 x {u16 x, u8 y, u8 strIndex}
static const int       MAG_TEXTBLKS  = 4;
static const uintptr_t MAG_TEXT_SEC  = 0x01D773A4;   // raw mngrp section: u16 count, u16 offsets[]
static const int       MAG_RAW_MAX   = 480;

// Information browser fields and globals.
static const int MAGO_TIPS_RECORD = 0x28;   // u16, the record on screen
// The page as the game drew it. 0x004D6B20 expands the record into the body
// buffer with numbers substituted and the 0x0B link markers REMOVED -- diverted
// instead into the position array, one 8-byte {u16 penX, u16 penY, u16 target}
// per link. The pen advances 0x10 per line break, so penY / 0x10 is the line the
// link's label sits on, and no page in the corpus puts two links on a line.
static const uintptr_t TIPS_TITLE_BUF = 0x01D84E50;   // strcpy'd verbatim
static const uintptr_t TIPS_BODY_BUF  = 0x01D7EC48;   // NUL-terminated
static const uintptr_t TIPS_LINK_POS  = 0x01D85658;   // 8 bytes each
static const uintptr_t TIPS_LINK_CNT  = 0x01D85650;   // u16
static const uintptr_t TIPS_CURSORS   = 0x01D83E4C;   // u8 per record id
static const uintptr_t TIPS_PARENT    = 0x01D84E4C;   // u16, 0xFFFF at the root
static const uintptr_t TIPS_PREVPAGE  = 0x01D8575A;   // u16
static const uintptr_t TIPS_NEXTPAGE  = 0x01D85758;   // u16
static const int       TIPS_LINE_H    = 0x10;
static const int       TIPS_RAW_MAX   = 2048;

// Exam module fields.
static const int SEEDO_STATE    = 0x10;   // u16
// NOT +0x20. That field is the FOOTER HINT ("X to quit"), which is what v0.26.0
// read and announced in place of every message the exam ever shows.
static const int SEEDO_REVIEW   = 0x2C;   // u8, 1 = reviewing a passed test
static const int SEEDO_TESTIDX  = 0x2D;   // u8, 0-based
static const int SEEDO_QUESTION = 0x2E;   // u8, 0..9
static const int SEEDO_CHOICE   = 0x2F;   // s8, 0 = YES, 1 = NO

// Globals.
//
// The question blob is loaded to a fixed slot in the menu heap: the exam's
// creator does `if (testIdx < 30) load mngrp section (0x60 + testIdx)` into
// [0x00B86D30] + 0x1F000. Layout is u16 count then count u16 offsets, and **the
// byte at each offset is the answer key**, with the text starting one later
// (0x004D5475). The key is read here only to prove the pointer arithmetic in
// the probe -- it is never spoken.
static const uintptr_t SEED_HEAP_PTR    = 0x00B86D30;
static const int       SEED_HEAP_OFFSET = 0x1F000;
static const uintptr_t SEED_CHOICE_CNT  = 0x01D7EAB8;   // u16
// **The text the game actually draws.** 0x004D4A80 expands whatever string the
// current state selected into this buffer -- names, GF names and numbers all
// substituted -- and NUL-terminates it (0x004D4CF5). The draw fn renders from
// here (0x004D596C). v0.26.0 read module+0x20 instead, which is the FOOTER hint:
// every message window announced "the Confirm button to quit" and nothing else.
static const uintptr_t SEED_TEXT_BUF    = 0x01D7DAB8;
// One 8-byte entry per answer slot: {u16 x, u16 y, u16 slot}. The y is what says
// where the labels start.
static const uintptr_t SEED_CHOICE_POS  = 0x01D7EB40;
static const int       SEED_LINE_HEIGHT = 0x10;
static const uintptr_t SEED_TESTS_PASS  = 0x01CFE98B;   // u8, 0..30
static const uintptr_t SEED_RANK_GATE_A = 0x01D2BA96;   // u8
static const uintptr_t SEED_RANK_GATE_B = 0x01CFE97A;   // u8, bit 0
static const uintptr_t SEED_RANK_POINTS = 0x01CFE9C8;   // s16

// Name buffers, from 0x0047EB50 and 0x0047E970. Characters 0 and 4 are the two
// the getter answers directly; every other id needs a party-slot indirection
// that no tutorial or exam string uses, so this reads the two it can trust.
static const uintptr_t SEED_NAME_CHAR0 = 0x01CFDC70;
static const uintptr_t SEED_NAME_CHAR4 = 0x01CFDC7C;
static const uintptr_t SEED_NAME_GF0   = 0x01CFDCA8;
static const int       SEED_NAME_GF_STRIDE = 68;        // (id-0x40)*17*4
static const int       SEED_NAME_BYTES = 12;

static const int SEED_RAW_MAX = 384;

// ---------------------------------------------------------------------------
// Everything the two screens need, copied out of the game in one guarded pass.
// **Raw bytes only.** Decoding happens afterwards, outside the __try, because
// MSVC will not let SEH and a non-trivial local share a function (C2712) and
// because a fault mid-decode would leave a half-built sentence.
// ---------------------------------------------------------------------------
struct TutRawText
{
    unsigned char question[SEED_RAW_MAX];
    int           questionLen;
    unsigned char message[SEED_RAW_MAX];
    int           messageLen;
    int           cutAtLine;      // where the answer labels begin, or -1
    unsigned char charName[2][SEED_NAME_BYTES + 1];
    unsigned char gfName[16][SEED_NAME_BYTES + 1];
};

static uint8_t* FindModuleByUpdateFn(uint32_t fn)
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)MM_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < MM_POOL_BASE || a >= MM_POOL_END) break;
            if ((a - MM_POOL_BASE) % 0x78 != 0) break;
            if (*(uint32_t*)(m + 0x08) == fn) return m;
            m = *(uint8_t* volatile*)m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

// 0x004C3090 reproduced. Two independent gates return -1 ("not a SeeD"), then
// the points are clamped to 100..3100 and divided by 100 -- so rank 1 is the
// floor, not zero, and a player with no points at all still reads as rank 1
// once the gates are open. Copying the clamp matters: computing the rank
// ourselves from the raw points would disagree with the salary screen.
static int SeedReadRank()
{
    __try {
        if (*(volatile uint8_t*)SEED_RANK_GATE_A == 0) return -1;
        if (*(volatile uint8_t*)SEED_RANK_GATE_B & 1) return -1;
        int pts = (int)*(volatile int16_t*)SEED_RANK_POINTS;
        if (pts < 100)  pts = 100;
        if (pts > 3100) pts = 3100;
        return pts / 100;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int SeedReadTestsPassed()
{
    __try {
        const int t = (int)*(volatile uint8_t*)SEED_TESTS_PASS;
        return (t < 0 || t > SEED_TEST_COUNT) ? 0 : t;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int SeedReadChoiceCount()
{
    __try {
        const int c = (int)*(volatile uint16_t*)SEED_CHOICE_CNT;
        return (c < 0 || c > 8) ? 0 : c;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// One guarded pass over everything textual. Both the question screen and the
// message windows read the SAME buffer, because both are written by the same
// pre-processor -- so there is one text path, and it is the one the game draws.
static bool TutCopyRaw(TutRawText& raw)
{
    memset(&raw, 0, sizeof(raw));
    raw.cutAtLine = -1;
    __try {
        const uint8_t* t = (const uint8_t*)SEED_TEXT_BUF;
        int i = 0;
        for (; i < SEED_RAW_MAX - 1; i++) {
            const uint8_t b = t[i];
            raw.message[i] = b;
            if (b == 0x00 || b == 0x01) { i++; break; }
        }
        raw.messageLen = i;

        // Where the answer labels start. The first choice's pen y divided by the
        // line height is its line number; the pen only ever moves down by a whole
        // line, so this is exact rather than a guess.
        const int cnt = (int)*(volatile uint16_t*)SEED_CHOICE_CNT;
        if (cnt > 0 && cnt <= 8) {
            const uint16_t y = *(const uint16_t*)(SEED_CHOICE_POS + 2);
            const int lineNo = (int)y / SEED_LINE_HEIGHT;
            if (lineNo > 0 && lineNo < 64) raw.cutAtLine = lineNo;
        }

        memcpy(raw.charName[0], (const void*)SEED_NAME_CHAR0, SEED_NAME_BYTES);
        memcpy(raw.charName[1], (const void*)SEED_NAME_CHAR4, SEED_NAME_BYTES);
        for (int g = 0; g < 16; g++)
            memcpy(raw.gfName[g], (const void*)(SEED_NAME_GF0 + g * SEED_NAME_GF_STRIDE),
                   SEED_NAME_BYTES);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Resolve a `0x03 nn` name id for TutExpand, through the one ladder the mod
// has (v0.38.0's savemap-backed decoder). This is the whole reason the Pet Pals
// pages said *"It's called... the character Strike!"*: the raw bytes there are
// `06 25 03 40 20 <Strike> 06 27 2E`, and `03 40` is ANGELO, which is not a
// party index at all -- the old lookup only understood `id - 0x30` over eight
// party slots, so Angelo, Griever, Boko and party slots 1,2,3,5,6,7 all came out
// as the placeholder.
//
// The returned pointer is valid until the next call; TutExpand consumes it
// immediately, one name at a time.
static const char* TutLookupName(unsigned char nameId)
{
    static char s_name[40];
    const uint8_t encoded[3] = { 0x03, nameId, 0x00 };
    std::string decoded = FF8TextDecode::Decode(encoded, sizeof(encoded));
    // An id the ladder does not know still reads as "[NameXX]"; hand back
    // nothing so the caller says "the character" rather than a bracketed code.
    if (decoded.empty() || decoded.find("[Name") != std::string::npos) return 0;
    snprintf(s_name, sizeof(s_name), "%s", decoded.c_str());
    return s_name;
}

// Decode the name buffers and hand back the two arrays the expander wants.
// Names are ordinary FF8 text, so they go through the same glyph table as
// everything else -- a player who renamed Ifrit to "Bob" hears Bob.
struct TutNameSet
{
    char  chars[8][20];
    char  gfs[16][20];
    const char* charPtr[8];
    const char* gfPtr[16];
};

static void TutBuildNames(const TutRawText& raw, TutNameSet& ns)
{
    memset(&ns, 0, sizeof(ns));
    TutTextInfo info;
    for (int i = 0; i < 8; i++) ns.charPtr[i] = 0;
    TutExpand(raw.charName[0], SEED_NAME_BYTES, 0, 0, ns.chars[0], sizeof(ns.chars[0]), &info, -1);
    TutExpand(raw.charName[1], SEED_NAME_BYTES, 0, 0, ns.chars[4], sizeof(ns.chars[4]), &info, -1);
    ns.charPtr[0] = ns.chars[0];
    ns.charPtr[4] = ns.chars[4];
    for (int g = 0; g < 16; g++) {
        TutExpand(raw.gfName[g], SEED_NAME_BYTES, 0, 0, ns.gfs[g], sizeof(ns.gfs[g]), &info, -1);
        ns.gfPtr[g] = ns.gfs[g];
    }
}

// ---------------------------------------------------------------------------
static bool s_tutActive     = false;
static int  s_tutState      = -1;
static int  s_tutCursor     = -999;
static int  s_tutTestPick   = -999;
static int  s_tutHelpCur    = -999;
static bool s_magActive     = false;
static int  s_magRecord     = -999;
static bool s_tipsActive    = false;
static int  s_tipsRecord    = -999;
static int  s_tipsCursor    = -999;
static bool s_seedActive    = false;
static int  s_seedState     = -1;
static int  s_seedQuestion  = -1;
static int  s_seedChoice    = -999;
static char s_tutLastSpoken[1600] = {0};   // v0.27.0: a magazine page is long, and a
                                          // truncated dedup key can match two different pages

static void ResetTutorialMenu()
{
    s_tutActive = false;  s_tutState = -1;
    s_tutCursor = -999;   s_tutTestPick = -999;
    s_tutHelpCur = -999;
    s_magActive = false;  s_magRecord = -999;
    s_tipsActive = false; s_tipsRecord = -999; s_tipsCursor = -999;
    s_seedActive = false; s_seedState = -1;
    s_seedQuestion = -1;  s_seedChoice = -999;
    s_tutLastSpoken[0] = '\0';
}

static void TutSpeak(const char* line)
{
    if (!line || line[0] == '\0') return;
    if (strcmp(line, s_tutLastSpoken) == 0) return;
    ScreenReader::Speak(line, true);
    snprintf(s_tutLastSpoken, sizeof(s_tutLastSpoken), "%s", line);
}

// ---------------------------------------------------------------------------
// The magazine viewer.
//
// **No pre-processing buffer here**, unlike the exam and the Information pages:
// the draw fn fetches each block straight out of the raw section every frame and
// renders it as-is, so no name or number substitution ever happens. That means
// the mod does its own expansion, which it was already doing anyway.
// ---------------------------------------------------------------------------
struct MagRawText
{
    unsigned char block[MAG_TEXTBLKS][MAG_RAW_MAX];
    int           blockLen[MAG_TEXTBLKS];
    int           blocks;
};

// v0.30.1 (#89): parameterised on WHERE the records and the string section
// live, because the Item menu's magazines are the same data in a different
// place -- see PollItemMagazine.
static bool MagCopyRawFrom(const uint8_t* recs, const uint8_t* sec,
                           int record, int maxRecord, MagRawText& raw)
{
    memset(&raw, 0, sizeof(raw));
    __try {
        if (!recs || !sec || record < 0 || record > maxRecord) return false;
        const uint8_t* rec = recs + MAG_REC_SIZE * record;
        const uint16_t cnt = *(const uint16_t*)sec;
        if (cnt == 0 || cnt > 400) return false;

        for (int b = 0; b < MAG_TEXTBLKS; b++) {
            const uint8_t idx = rec[MAG_TEXTBLK + b * 4 + 3];
            if (idx == 0xFF) break;            // the block list is 0xFF-terminated
            if (idx >= cnt) break;
            const uint8_t* t = sec + *(const uint16_t*)(sec + 2 + idx * 2);
            int i = 0;
            for (; i < MAG_RAW_MAX - 1; i++) {
                const uint8_t c = t[i];
                raw.block[b][i] = c;
                if (c == 0x00 || c == 0x01) { i++; break; }
            }
            raw.blockLen[b] = i;
            raw.blocks = b + 1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

static bool MagCopyRaw(int record, MagRawText& raw)
{
    const uint8_t* recs = nullptr;
    __try { recs = *(const uint8_t* volatile*)MAG_RECS_PTR; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return MagCopyRawFrom(recs, (const uint8_t*)MAG_TEXT_SEC, record, 68, raw);
}

static bool FillMagazineView(uint8_t* mod, MagazineView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state  = *(uint16_t*)(mod + MAGO_STATE);
        v.record = (int)*(uint32_t*)(mod + MAGO_RECORD);
        v.first  = (int)*(volatile uint8_t*)MAG_FIRST_REC;
        v.last   = (int)*(volatile uint8_t*)MAG_LAST_REC;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// The text half, outside any __try so the expander is not inside SEH.
static void FillMagazineTextFrom(const MagRawText& raw, char* out, size_t n)
{
    if (out && n) out[0] = '\0';

    TutRawText names;
    TutNameSet ns;
    if (TutCopyRaw(names)) TutBuildNames(names, ns);
    else memset(&ns, 0, sizeof(ns));

    TutTextInfo info;
    char piece[MAG_RAW_MAX * 2];
    for (int b = 0; b < raw.blocks; b++) {
        if (raw.blockLen[b] <= 0) continue;
        memset(&info, 0, sizeof(info));
        TutExpand(raw.block[b], raw.blockLen[b], ns.charPtr, ns.gfPtr,
                  piece, sizeof(piece), &info, -1, TutLookupName);
        if (!piece[0]) continue;
        if (out[0]) TutAppend(out, n, " ");
        TutAppend(out, n, piece);
        // Each block is a separate line of the page, so it gets a stop of its
        // own -- the heading, the counter and the body are three sentences, not
        // one run-on.
        const size_t l = strlen(out);
        if (l > 0 && out[l-1] != '.' && out[l-1] != '!' && out[l-1] != '?')
            TutAppend(out, n, ".");
    }
}

static void FillMagazineText(MagazineView& v)
{
    MagRawText raw;
    if (!MagCopyRaw(v.record, raw)) return;
    FillMagazineTextFrom(raw, v.text, sizeof(v.text));
}

// The character names, rebuilt each poll and pointed at by the view. Kept as a
// file static because building them needs the expander, which must not run
// inside the __try that reads the module.
static TutNameSet s_tutNames;

// ---------------------------------------------------------------------------
// The Information browser.
// ---------------------------------------------------------------------------
struct TipsRaw
{
    unsigned char title[256];
    int           titleLen;
    unsigned char body[TIPS_RAW_MAX];
    int           bodyLen;
    int           linkLine[TIPS_MAX_LINKS];
    int           linkCount;
};

static bool TipsCopyRaw(int record, TipsRaw& raw)
{
    memset(&raw, 0, sizeof(raw));
    __try {
        const uint8_t* t = (const uint8_t*)TIPS_TITLE_BUF;
        int i = 0;
        for (; i < 255; i++) { const uint8_t c = t[i]; raw.title[i] = c; if (!c) break; }
        raw.titleLen = i;

        const uint8_t* b = (const uint8_t*)TIPS_BODY_BUF;
        for (i = 0; i < TIPS_RAW_MAX - 1; i++) {
            const uint8_t c = b[i];
            raw.body[i] = c;
            if (c == 0x00 || c == 0x01) { i++; break; }
        }
        raw.bodyLen = i;

        int n = (int)*(volatile uint16_t*)TIPS_LINK_CNT;
        if (n < 0) n = 0;
        if (n > TIPS_MAX_LINKS) n = TIPS_MAX_LINKS;
        for (int k = 0; k < n; k++) {
            const uint16_t y = *(const uint16_t*)(TIPS_LINK_POS + k * 8 + 2);
            raw.linkLine[k] = (int)y / TIPS_LINE_H;
        }
        raw.linkCount = n;
        (void)record;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

static bool FillTipsView(uint8_t* mod, TipsView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state     = *(uint16_t*)(mod + MAGO_STATE);
        v.record    = (int)*(uint16_t*)(mod + MAGO_TIPS_RECORD);
        v.hasParent = *(volatile uint16_t*)TIPS_PARENT   != 0xFFFF;
        v.hasPrev   = *(volatile uint16_t*)TIPS_PREVPAGE != 0xFFFF;
        v.hasNext   = *(volatile uint16_t*)TIPS_NEXTPAGE != 0xFFFF;
        // The cursor is stored PER RECORD, so backing out of a topic and
        // returning puts you back on the link you left from.
        v.cursor    = (v.record >= 0 && v.record < 0x1000)
                    ? (int)*(volatile uint8_t*)(TIPS_CURSORS + v.record) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Split the drawn body into lines and sort them: a line a link sits on IS that
// link's label, and everything else is prose. Outside any __try, because the
// expander must not run inside SEH.
static void FillTipsText(TipsView& v)
{
    TipsRaw raw;
    if (!TipsCopyRaw(v.record, raw)) return;

    TutRawText names;
    TutNameSet ns;
    if (TutCopyRaw(names)) TutBuildNames(names, ns);
    else memset(&ns, 0, sizeof(ns));

    TutTextInfo info;
    memset(&info, 0, sizeof(info));
    if (raw.titleLen > 0)
        TutExpand(raw.title, raw.titleLen, ns.charPtr, ns.gfPtr,
                  v.title, sizeof(v.title), &info, -1, TutLookupName);

    v.linkCount = raw.linkCount;
    if (v.cursor < 0 || v.cursor >= v.linkCount) v.cursor = 0;

    // Walk the body one line at a time. Line breaks are 0x02 and the game's
    // expander preserved them, so the mod's line numbering and the pen's agree.
    int line = 0, start = 0;
    char piece[TIPS_LABEL_MAX * 6];
    for (int i = 0; i <= raw.bodyLen; i++) {
        const bool end = (i == raw.bodyLen) || raw.body[i] == 0x00 || raw.body[i] == 0x01;
        if (!end && raw.body[i] != 0x02) continue;

        const int len = i - start;
        if (len > 0) {
            memset(&info, 0, sizeof(info));
            TutExpand(raw.body + start, len, ns.charPtr, ns.gfPtr,
                      piece, sizeof(piece), &info, -1, TutLookupName);
            if (piece[0]) {
                int slot = -1;
                for (int k = 0; k < raw.linkCount; k++)
                    if (raw.linkLine[k] == line) { slot = k; break; }
                if (slot >= 0 && slot < TIPS_MAX_LINKS) {
                    snprintf(v.links[slot], TIPS_LABEL_MAX, "%s", piece);
                } else {
                    // A table row gets a stop on both sides of it, so the Battle
                    // Report reads "Walked, 109751. Battles, 41." rather than
                    // running the value of one row into the label of the next.
                    if (v.body[0]) {
                        const size_t l = strlen(v.body);
                        const char last = v.body[l-1];
                        if (info.columns && last != '.' && last != '!' && last != '?')
                            TutAppend(v.body, sizeof(v.body), ".");
                        TutAppend(v.body, sizeof(v.body), " ");
                    }
                    TutAppend(v.body, sizeof(v.body), piece);
                    if (info.columns) {
                        const size_t l = strlen(v.body);
                        const char last = l ? v.body[l-1] : '.';
                        // A trailing comma means the value was empty -- the game
                        // had nothing to substitute -- and ",." is a stutter.
                        if (last != '.' && last != '!' && last != '?' && last != ',')
                            TutAppend(v.body, sizeof(v.body), ".");
                    }
                }
            }
        }
        if (end) break;
        line++;
        start = i + 1;
    }
}

static bool FillTutorialView(uint8_t* mod, TutorialView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state      = *(uint16_t*)(mod + TUTO_STATE);
        v.testPick   = (int)*(int8_t*)(mod + TUTO_TESTPICK);
        v.cursor     = (int)*(int8_t*)(mod + TUTO_CURSOR);
        v.helpCursor = (int)*(uint8_t*)(mod + TUTO_HELPCUR);
        v.helpCount  = (int)*(uint8_t*)(mod + TUTO_HELPCNT);
        v.helpDescriptor = -1;
        if (v.helpCursor >= 0 && v.helpCursor < TUT_HELP_MAX &&
            v.helpCursor < v.helpCount)
            v.helpDescriptor = (int)*(uint8_t*)(mod + TUTO_HELPMAP + v.helpCursor);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    v.testsPassed = SeedReadTestsPassed();
    v.seedRank    = SeedReadRank();

    TutRawText raw;
    if (TutCopyRaw(raw)) TutBuildNames(raw, s_tutNames);
    v.charNames = s_tutNames.charPtr;
    return true;
}

static bool FillSeedExamFields(uint8_t* mod, SeedExamView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state         = *(uint16_t*)(mod + SEEDO_STATE);
        v.reviewMode    = *(uint8_t*)(mod + SEEDO_REVIEW) == 1;
        v.testIndex     = (int)*(uint8_t*)(mod + SEEDO_TESTIDX);
        v.questionIndex = (int)*(uint8_t*)(mod + SEEDO_QUESTION);
        v.choice        = (int)*(int8_t*)(mod + SEEDO_CHOICE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    v.choiceCount = SeedReadChoiceCount();
    return true;
}

// Expand the drawn buffer. Kept out of TutCopyRaw because the expander must not
// run inside a __try, and out of the poll because both callers want it.
//
// The names are still passed in even though 0x004D4A80 has already substituted
// them: it costs nothing, and if a code ever reaches this buffer unexpanded the
// mod says a name rather than dropping a word out of the middle of a question.
static void FillSeedExamText(SeedExamView& v)
{
    TutRawText raw;
    if (!TutCopyRaw(raw)) return;

    TutNameSet ns;
    TutBuildNames(raw, ns);

    TutTextInfo info;
    memset(&info, 0, sizeof(info));
    if (raw.messageLen > 0) {
        TutExpand(raw.message, raw.messageLen, ns.charPtr, ns.gfPtr,
                  v.text, sizeof(v.text), &info, raw.cutAtLine, TutLookupName);
        snprintf(v.message, sizeof(v.message), "%s", v.text);
        // The answer words come off the screen's own answer line, because they
        // are not always YES then NO -- the "Really?" confirmation lists NO
        // first, and some screens offer END or GO BACK.
        v.labelCount = info.labelCount;
        for (int i = 0; i < 4 && i < info.labelCount; i++)
            snprintf(v.labels[i], sizeof(v.labels[i]), "%s", info.labels[i]);
    }
}

// ---------------------------------------------------------------------------
// The poll.
//
// The exam is checked FIRST and unconditionally. When it is up, the Tutorial
// module is still in the pool one state behind it, and announcing that stale
// list under a live question would be the worst possible failure on the one
// screen where a wrong reading changes the answer the player gives.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// v0.30.0 (#89): THE MAGAZINE VIEWER, IDENTIFIED BY ITS MODULE.
//
// Aaron: *"We haven't added support for magazines such as Weapons Monthly and
// Pet Pals, which are accessed via the item menu."*
//
// The viewer itself was already written -- v0.27.0 built it for the Tutorial's
// Battle Operation / Card Game Rules / Icon Explanation, and its own comment
// noted that records 0..42 are the field magazines, "which share this viewer but
// are not reached from here". **They share the viewer because it is one module**:
// all three creators (0x004C8FF0 / 0x004C9820 / 0x004C9890) call
// 0x004BE540(0x004C9060, 0x004C9330), and 0x004C99B0 is a generic setter that
// takes an index into the table at [0x01D2BB3C] and writes {first, last} into
// 0x01D7D3A5/A6. The Tutorial uses entries 0, 1 and 2 of that table. Nothing
// about the viewer is Tutorial-specific.
//
// What kept the Item menu's magazines silent was the GATE, not the reader: the
// poll only ran when the active-submenu byte held one of the dispatch ids the
// TUTORIAL pushes. **A module found by its update function is already positive
// identification** -- either 0x004C9060 is in the pool or it is not -- so the id
// gate added nothing except a way to be wrong about where the viewer was opened
// from. This runs before it now.
//
// Returns true when it handled the frame.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// v0.30.1 (#89): THE ITEM MENU'S MAGAZINES ARE NOT A SEPARATE MODULE.
//
// v0.30.0 assumed they opened the Tutorial's viewer and only the gate was in the
// way. **The BAT log says otherwise, in its own focus trace:**
//
//     Item focus: 5 -> 81 -> 82 -> 83 -> 84 -> 85     (opening)
//     Item focus: 85 -> 86 -> 87 -> 85                (page left)
//     Item focus: 85 -> 88 -> 89 -> 85                (page right)
//
// Those are states 0x51..0x59 of the **Item** state machine. There is no second
// module; hoisting the viewer's gate could never have helped, and did not.
//
// The data is the same shape in a different place:
//
//   [0x01D2BB2C] + magId*4     the magazine's page range: +2 first, +3 last
//                              (0x004FB60A compares them; 0x004FB68E wraps)
//   [esi+0x65]                 magId          (0x004FB611)
//   [esi+0x52]                 the page on screen  (0x004FCA96)
//   [0x01D2BB6C] + page*68     the page record     (0x004FCAA0)
//   record +0x34               4 x {u16 x, u8 y, u8 strIndex}, 0xFF-terminated
//   [0x00B86D30] + 0x1F000     the string section: u16 count, u16 offsets[]
//                              (0x004FD746..0x004FD75A)
//
// -- which is byte for byte the layout v0.27.0 already decodes for the Tutorial
// magazines, so MagCopyRawFrom does both.
//
// Returns true when it handled the frame.
// ---------------------------------------------------------------------------
static const uintptr_t IMAG_RANGE_TBL = 0x01D2BB2C;   // -> {?, ?, first, last} x N
static const uintptr_t IMAG_RECS_PTR  = 0x01D2BB6C;   // -> records, 68 bytes each
static const uintptr_t IMAG_ARCHIVE   = 0x00B86D30;   // -> menu archive; text at +0x1F000
static const int       IMAG_TEXT_OFF  = 0x1F000;
static const int IMAGO_STATE = 0x10;
static const int IMAGO_PAGE  = 0x52;
static const int IMAGO_ID    = 0x65;
static const int IMAG_STATE_LO = 0x51, IMAG_STATE_HI = 0x59, IMAG_STATE_READ = 0x55;

// v0.31.1 (#90): THE "ITEMS FOR REMODELING" PANEL.
//
// The page says "Items for remodeling" and then lists what the Junk Shop needs,
// with a count against each. The mod read the heading -- it is text block 2 of
// the record -- and then stopped, which is the v0.28.1 empty-page failure again:
// a heading followed by silence is indistinguishable from the mod having broken.
// On a Weapons Monthly that list is the practical point of the page.
//
// It is not in the record and not in the GCW buffer's usable form. It is keyed
// off the record's weapon id:
//
//   mmag record +0x18        the weapon id (records 0..27 carry a permutation
//                            of 0..27; 0xFF on the other magazines)
//   [0x01D2BB58]             -> mwepon.bin, loaded by the Item creator at
//                            0x004F8023 next to mmag.bin. **33 records of 12
//                            bytes**, and record W is weapon W.
//   record +0x04             four {u8 itemId, u8 count} pairs, ending at the
//                            first zero count.
//
// Checked against the page in Aaron's screenshot: the Maverick is mmag record 9,
// weapon id 8, and mwepon record 8 reads {155 x1, 127 x1} = Dragon Fin 1,
// Spider Web 1 -- the two lines on that page, in that order.
//
// Spoken as "name, count" per the v0.28.1 column rule, because that is what the
// panel is: a label column and a number column.
static const uintptr_t IMAG_WEPON_PTR   = 0x01D2BB58;
static const int       IMAG_WEPON_REC   = 12;
static const int       IMAG_WEPON_MAX   = 33;
static const int       IMAG_WEPON_ITEMS = 0x04;
static const int       IMAG_REC_WEAPON  = 0x18;   // within the 68-byte mmag record
static const int       IMAG_REMODEL_MAX = 4;

struct MagRemodel
{
    int     count;
    uint8_t id[IMAG_REMODEL_MAX];
    uint8_t qty[IMAG_REMODEL_MAX];
};

static bool MagReadRemodel(const uint8_t* recs, int page, MagRemodel& out)
{
    memset(&out, 0, sizeof(out));
    __try {
        if (!recs || page < 0) return false;
        const uint8_t wid = recs[page * MAG_REC_SIZE + IMAG_REC_WEAPON];
        if (wid >= IMAG_WEPON_MAX) return false;      // 0xFF on the other magazines
        const uint8_t* wep = *(const uint8_t* volatile*)IMAG_WEPON_PTR;
        if (!wep) return false;
        const uint8_t* rec = wep + wid * IMAG_WEPON_REC;
        for (int i = 0; i < IMAG_REMODEL_MAX; i++) {
            const uint8_t id = rec[IMAG_WEPON_ITEMS + i * 2];
            const uint8_t q  = rec[IMAG_WEPON_ITEMS + i * 2 + 1];
            if (q == 0) break;                        // the list ends at a zero count
            out.id[out.count] = id;
            out.qty[out.count] = q;
            out.count++;
        }
        return out.count > 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// std::string-free, so it can sit anywhere.
static void MagAppendRemodel(const MagRemodel& r, char* out, size_t n)
{
    for (int i = 0; i < r.count; i++) {
        const char* nm = GetItemName(r.id[i]);
        const size_t l = strlen(out);
        if (l + 1 >= n) return;
        snprintf(out + l, n - l, " %s, %u.",
                 (nm && nm[0]) ? nm : "Unknown item", (unsigned)r.qty[i]);
    }
}

static bool  s_imagActive = false;
static int   s_imagPage   = -999;
static int   s_imagArtRec = -1;   // v0.31.0: the record "/" would describe

struct ItemMagView
{
    int  state, page, magId, first, last;
    const uint8_t* recs;
    const uint8_t* sec;
};

static bool FillItemMagView(uint8_t* mod, ItemMagView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state = (int)*(volatile uint16_t*)(mod + IMAGO_STATE);
        if (v.state < IMAG_STATE_LO || v.state > IMAG_STATE_HI) return false;
        v.page  = (int)*(volatile uint8_t*)(mod + IMAGO_PAGE);
        v.magId = (int)*(volatile uint8_t*)(mod + IMAGO_ID);

        const uint8_t* range = *(const uint8_t* volatile*)IMAG_RANGE_TBL;
        if (!range) return false;
        v.first = (int)range[v.magId * 4 + 2];
        v.last  = (int)range[v.magId * 4 + 3];
        if (v.last < v.first) return false;
        if (v.page < v.first || v.page > v.last) return false;

        v.recs = *(const uint8_t* volatile*)IMAG_RECS_PTR;
        const uint8_t* arc = *(const uint8_t* volatile*)IMAG_ARCHIVE;
        if (!v.recs || !arc) return false;
        v.sec = arc + IMAG_TEXT_OFF;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// v0.31.0 (#90): "/" on a magazine page describes the picture.
//
// The page text tells the player what the weapon IS and what it is for. It
// never says what it LOOKS like, and on a magazine whose whole layout is
// "picture on the left, prose on the right" that is half the page missing.
//
// Silent -- returns false -- on any page with no weapon plate, so the key falls
// through to whatever else wants it. Records 28 and up are the other magazines.
static bool MagazineSpeakArt()
{
    if (!s_imagActive || s_imagArtRec < 0) return false;
    if (s_imagArtRec >= MAG_ART_COUNT) {
        ScreenReader::Speak("No picture description for this page", true);
        Log::Menu("[TutorialTTS] magazine art: record %d has no plate", s_imagArtRec);
        return true;
    }
    const MagArtEntry& e = MAG_ART[s_imagArtRec];
    char out[640];
    snprintf(out, sizeof(out), "%s. %s", e.name, e.look);
    ScreenReader::Speak(out, true);
    Log::Menu("[TutorialTTS] magazine art rec=%d: \"%.200s\"", s_imagArtRec, out);
    return true;
}

static bool PollItemMagazine()
{
    // v0.30.2 (#89): **this asked the raw pool walk, and the walk does not find
    // the Item module.** The v0.30.1 BAT proves it in the line right above where
    // this should have logged: the GF target list read every row correctly via
    // "[slot2 (walk found nothing)]", so the module was at pMenuStateA+0x21E the
    // whole time and this function returned at its first line without ever
    // reaching a log statement. **I fixed that identification one function over
    // and left this one on the broken version.**
    const char* how = "";
    uint8_t* mod = ItemModuleBaseInStates(IMAG_STATE_LO, IMAG_STATE_HI, &how);
    if (!mod) {
        if (s_imagActive) { s_imagActive = false; s_imagPage = -999; s_imagArtRec = -1; }
        return false;
    }

    ItemMagView v;
    if (!FillItemMagView(mod, v)) {
        if (s_imagActive) {
            s_imagActive = false; s_imagPage = -999; s_imagArtRec = -1;
            Log::Menu("[TutorialTTS] item magazine closed (view no longer readable)");
        }
        return false;
    }

    // Only the reading state is steady. 0x51..0x54 are the open animation and
    // 0x56..0x59 are the page slides, and a slide re-enters 0x55 when it lands
    // -- so speaking on arrival at 0x55 gets each page exactly once.
    if (v.state != IMAG_STATE_READ) return true;

    if (!s_imagActive) {
        s_imagActive = true;
        s_imagPage = -999;
        s_tutLastSpoken[0] = '\0';
        Log::Menu("[TutorialTTS] item magazine open [%s]: id=%d pages %d..%d "
                  "recs=%p sec=%p",
                  how, v.magId, v.first, v.last, (const void*)v.recs,
                  (const void*)v.sec);
    }
    s_imagArtRec = v.page;
    if (v.page == s_imagPage) return true;
    s_imagPage = v.page;

    MagRawText raw;
    if (!MagCopyRawFrom(v.recs, v.sec, v.page, 255, raw)) {
        Log::Menu("[TutorialTTS] item magazine page %d: could not read the record",
                  v.page);
        return true;
    }
    char line[1600];
    FillMagazineTextFrom(raw, line, sizeof(line));
    if (!line[0]) return true;

    // The page's own text stops at the heading "Items for remodeling"; the list
    // under it is drawn from the weapon table, so it is appended here.
    MagRemodel rm;
    if (MagReadRemodel(v.recs, v.page, rm)) MagAppendRemodel(rm, line, sizeof(line));

    // The page counter is drawn beside the headline and is part of the stored
    // text, so it is not added here. What the stored text does NOT say is that
    // you have reached the end, and the footer hint on screen only names the
    // scroll keys.
    const int page = v.page - v.first + 1, pages = v.last - v.first + 1;
    char out[1700];
    snprintf(out, sizeof(out), "%s%s", line,
             (pages > 0 && page == pages) ? " Last page." : "");
    ScreenReader::Speak(out, true);
    snprintf(s_tutLastSpoken, sizeof(s_tutLastSpoken), "%s", out);
    Log::Menu("[TutorialTTS] item magazine id=%d page %d (%d of %d, recs %d..%d) : \"%.400s\"",
              v.magId, v.page, page, pages, v.first, v.last, out);
    return true;
}

static bool PollMagazineViewer()
{
    uint8_t* mag = FindModuleByUpdateFn(MAG_UPDATE_FN);
    if (!mag) {
        if (s_magActive) { s_magActive = false; s_magRecord = -999; }
        return false;
    }

    MagazineView mv;
    if (!FillMagazineView(mag, mv)) return true;
    if (mv.state != MAG_STATE_PAGE) return true;

    if (!s_magActive) {
        s_magActive = true;
        s_magRecord = -999;
        s_tutLastSpoken[0] = '\0';
        Log::Menu("[TutorialTTS] magazine module at 0x%08X (records %d..%d)",
                  (unsigned)(uintptr_t)mag, mv.first, mv.last);
    }
    if (mv.record == s_magRecord) return true;
    s_magRecord = mv.record;

    FillMagazineText(mv);
    char line[1600];
    line[0] = '\0';
    MagAnnouncePage(mv, line, sizeof(line));
    if (line[0] == '\0') return true;
    ScreenReader::Speak(line, true);
    snprintf(s_tutLastSpoken, sizeof(s_tutLastSpoken), "%s", line);
    Log::Menu("[TutorialTTS] magazine rec=%d (%d..%d) : \"%.400s\"",
              mv.record, mv.first, mv.last, line);
    return true;
}

static void PollTutorialMenu()
{
    if (!pMenuStateA) return;

    // Checked BEFORE the active-submenu gate: the viewer is reached from the
    // Item menu as well as from the Tutorial, and the module in the pool is the
    // evidence for that, not the dispatch id.
    if (PollMagazineViewer()) return;

    // v0.30.1 (#89): the Item menu's own magazine states. Same data, different
    // module -- and no module of its own at all.
    if (PollItemMagazine()) return;

    uint8_t sub = 0xFF;
    __try { sub = *((uint8_t*)pMenuStateA + JUNC_ACTIVE_OFFSET); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    // v0.27.0: the Tutorial's rows push four more dispatch ids on top of it --
    // 21 (Information), 25/26/31 (the magazine viewer) and 23 (the exam) -- and
    // the active-submenu byte follows whichever is on top.
    if (sub != TUTORIAL_SUBSYSTEM_ID && sub != SEEDTEST_SUBSYSTEM_ID &&
        sub != MAGAZINE_SUBSYSTEM_A && sub != MAGAZINE_SUBSYSTEM_B &&
        sub != MAGAZINE_SUBSYSTEM_C && sub != TIPS_SUBSYSTEM_ID) {
        if (s_tutActive || s_seedActive || s_magActive) ResetTutorialMenu();
        return;
    }

    // Information, checked first for the same reason the exam and the magazine
    // are: it is pushed on top of the Tutorial module, which stays in the pool
    // beneath it in a state of its own.
    uint8_t* tips = FindModuleByUpdateFn(TIPS_UPDATE_FN);
    if (tips) {
        TipsView tv;
        if (!FillTipsView(tips, tv)) return;
        if (tv.state != TIPS_STATE_PAGE) return;

        if (!s_tipsActive) {
            s_tipsActive = true;
            s_tipsRecord = -999; s_tipsCursor = -999;
            s_tutLastSpoken[0] = '\0';
            Log::Menu("[TutorialTTS] tips module at 0x%08X", (unsigned)(uintptr_t)tips);
        }

        char line[1900];
        line[0] = '\0';
        if (tv.record != s_tipsRecord) {
            FillTipsText(tv);
            TipsAnnouncePage(tv, line, sizeof(line));
        } else if (tv.cursor != s_tipsCursor) {
            FillTipsText(tv);
            TipsAnnounceLink(tv, line, sizeof(line));
        } else {
            return;
        }
        s_tipsRecord = tv.record;
        s_tipsCursor = tv.cursor;

        if (line[0] == '\0') return;
        ScreenReader::Speak(line, true);
        snprintf(s_tutLastSpoken, sizeof(s_tutLastSpoken), "%s", line);
        Log::Menu("[TutorialTTS] tips rec=%d link=%d/%d prev=%d next=%d : \"%.400s\"",
                  tv.record, tv.cursor, tv.linkCount, (int)tv.hasPrev, (int)tv.hasNext, line);
        return;
    }
    if (s_tipsActive) { s_tipsActive = false; s_tipsRecord = -999; s_tipsCursor = -999; }

    uint8_t* seed = FindModuleByUpdateFn(SEED_UPDATE_FN);
    if (seed) {
        SeedExamView v;
        if (!FillSeedExamFields(seed, v)) return;

        const bool isQ   = (v.state == SEED_STATE_QUESTION);
        const bool isMsg = SeedStateIsMessage((int)v.state);
        if (!isQ && !isMsg) { s_seedState = (int)v.state; return; }

        if (!s_seedActive) {
            s_seedActive = true;
            s_seedQuestion = -1; s_seedChoice = -999; s_tutLastSpoken[0] = '\0';
            Log::Menu("[TutorialTTS] exam module at 0x%08X (pool slot %d)",
                      (unsigned)(uintptr_t)seed,
                      (int)(((uintptr_t)seed - MM_POOL_BASE) / 0x78));
        }

        char line[640];
        line[0] = '\0';
        FillSeedExamText(v);
        if (isQ) {
            // A new question reads in full; moving between Yes and No reads only
            // the word. Ten questions of re-reading the stem would be unusable.
            if (v.questionIndex != s_seedQuestion || (int)v.state != s_seedState) {
                SeedAnnounceQuestion(v, line, sizeof(line));
                s_tutLastSpoken[0] = '\0';   // the same question can recur in review
            } else if (v.choice != s_seedChoice) {
                SeedAnnounceChoice(v, line, sizeof(line));
            }
            s_seedQuestion = v.questionIndex;
            s_seedChoice   = v.choice;
        } else {
            SeedAnnounceMessage(v, line, sizeof(line));
            if (v.choice != s_seedChoice) s_seedChoice = v.choice;
            s_seedQuestion = -1;
        }
        s_seedState = (int)v.state;

        // **Compare before speaking AND before logging.** v0.26.0 deduped only
        // the speech, so a message window that never changes wrote a [TutorialTTS]
        // line every frame -- forty identical entries in one second of Aaron's BAT
        // log, which buries whatever the next real event was.
        if (line[0] == '\0' || strcmp(line, s_tutLastSpoken) == 0) return;
        TutSpeak(line);
        Log::Menu("[TutorialTTS] exam state=%u test=%d q=%d choice=%d : \"%s\"",
                  (unsigned)v.state, v.testIndex, v.questionIndex, v.choice, line);
        return;
    }
    if (s_seedActive) { s_seedActive = false; s_seedState = -1; s_seedQuestion = -1; }

    uint8_t* mod = FindModuleByUpdateFn(TUT_UPDATE_FN);
    if (!mod) { if (s_tutActive) ResetTutorialMenu(); return; }
    if (!s_tutActive) {
        s_tutActive = true;
        s_tutState = -1; s_tutCursor = -999; s_tutTestPick = -999;
        s_tutLastSpoken[0] = '\0';
        Log::Menu("[TutorialTTS] module at 0x%08X (pool slot %d)",
                  (unsigned)(uintptr_t)mod, (int)(((uintptr_t)mod - MM_POOL_BASE) / 0x78));
    }

    TutorialView v;
    if (!FillTutorialView(mod, v)) return;

    char line[640];
    line[0] = '\0';
    if (v.state == TUT_STATE_LIST) {
        if (v.cursor != s_tutCursor || s_tutState != TUT_STATE_LIST)
            TutAnnounceRow(v, line, sizeof(line));
        s_tutCursor = v.cursor;
        s_tutTestPick = -999;
        s_tutHelpCur = -999;
    } else if (v.state == TUT_STATE_HELPLIST) {
        // Online Help is a second panel inside this same module, not a module of
        // its own -- row 1's action byte 0xFF sets state 24 rather than pushing
        // anything. Arrival says how many topics the story has unlocked so far.
        if (s_tutState != TUT_STATE_HELPLIST) {
            TutAnnounceHelpArrival(v.helpCount, line, sizeof(line));
            char row[256];
            TutAnnounceHelpRow(v.helpDescriptor, v.charNames, v.helpCursor,
                               v.helpCount, row, sizeof(row));
            if (row[0]) { TutAppend(line, sizeof(line), ". "); TutAppend(line, sizeof(line), row); }
        } else if (v.helpCursor != s_tutHelpCur) {
            TutAnnounceHelpRow(v.helpDescriptor, v.charNames, v.helpCursor,
                               v.helpCount, line, sizeof(line));
        }
        s_tutHelpCur = v.helpCursor;
        s_tutCursor = -999;
    } else if (v.state == TUT_STATE_TESTPICK) {
        if (v.testPick != s_tutTestPick || s_tutState != TUT_STATE_TESTPICK)
            TutAnnounceTestPick(v, line, sizeof(line));
        s_tutTestPick = v.testPick;
        s_tutCursor = -999;
    } else {
        s_tutState = (int)v.state;
        return;
    }
    s_tutState = (int)v.state;

    if (line[0] == '\0') return;
    TutSpeak(line);
    // v0.28.0: log the cursor that actually moved. Until now the Online Help
    // lines all read `row=1` -- the SEVEN-ROW list's cursor -- while the help
    // cursor was the thing changing, which would have misled the next BAT read.
    Log::Menu("[TutorialTTS] state=%u row=%d help=%d pick=%d passed=%d rank=%d : \"%s\"",
              (unsigned)v.state, v.cursor, v.helpCursor, v.testPick,
              v.testsPassed, v.seedRank, line);
}

// ---------------------------------------------------------------------------
// Number keys.
//   0  the question again, or the row's help line
//   1  where am I
//   2  the two answers, or the SeeD standing
// ---------------------------------------------------------------------------
static void TutorialNumberKeys()
{
    if (!s_tutActive && !s_seedActive && !s_magActive && !s_tipsActive) return;

    int key = -1;
    for (int k = 0; k <= 2; k++)
        if (GetAsyncKeyState('0' + k) & 1) { key = k; break; }
    if (key < 0) return;

    char buf[1600];
    buf[0] = '\0';

    uint8_t* tips = FindModuleByUpdateFn(TIPS_UPDATE_FN);
    if (tips) {
        TipsView tv;
        if (!FillTipsView(tips, tv)) return;
        FillTipsText(tv);
        if      (key == 0) TipsAnnounceAll(tv, buf, sizeof(buf));
        else if (key == 1) TipsAnnounceNav(tv, buf, sizeof(buf));
        else               TipsAnnounceLinks(tv, buf, sizeof(buf));
        if (buf[0] == '\0') return;
        ScreenReader::Speak(buf, true);
        Log::Menu("[TutorialTTS] key=%d tips rec=%d: %.300s", key, tv.record, buf);
        return;
    }

    // v0.27.0: the magazine viewer, checked first for the same reason the poll
    // checks it first -- it is on top when it is up.
    uint8_t* mag = FindModuleByUpdateFn(MAG_UPDATE_FN);
    if (mag) {
        MagazineView mv;
        if (!FillMagazineView(mag, mv)) return;
        if (key == 0) { FillMagazineText(mv); MagAnnouncePage(mv, buf, sizeof(buf)); }
        else          MagAnnouncePosition(mv, buf, sizeof(buf));
        if (buf[0] == '\0') return;
        ScreenReader::Speak(buf, true);
        Log::Menu("[TutorialTTS] key=%d magazine rec=%d: %.200s", key, mv.record, buf);
        return;
    }

    uint8_t* seed = FindModuleByUpdateFn(SEED_UPDATE_FN);
    if (seed) {
        SeedExamView v;
        if (!FillSeedExamFields(seed, v)) return;
        const bool isQ = (v.state == SEED_STATE_QUESTION);
        FillSeedExamText(v);
        if      (key == 0) { if (isQ) SeedAnnounceText(v, buf, sizeof(buf));
                             else     SeedAnnounceMessage(v, buf, sizeof(buf)); }
        else if (key == 1) SeedAnnouncePosition(v, buf, sizeof(buf));
        else {
            TutAppend(buf, sizeof(buf), "Answer ");
            TutAppend(buf, sizeof(buf), SeedChoiceWord(v, v.choice));
            TutAppend(buf, sizeof(buf), ". Left and right change it, Confirm answers");
        }
    } else {
        uint8_t* mod = FindModuleByUpdateFn(TUT_UPDATE_FN);
        if (!mod) return;
        TutorialView v;
        if (!FillTutorialView(mod, v)) return;
        if (v.state == TUT_STATE_HELPLIST) {
            if (key == 2) TutAnnounceHelpArrival(v.helpCount, buf, sizeof(buf));
            else          TutAnnounceHelpRow(v.helpDescriptor, v.charNames,
                                             v.helpCursor, v.helpCount, buf, sizeof(buf));
        }
        else if (key == 0) TutAnnounceRow(v, buf, sizeof(buf));
        else if (key == 1) TutAnnouncePosition(v, buf, sizeof(buf));
        else               TutAnnounceStanding(v, buf, sizeof(buf));
    }

    if (buf[0] == '\0') return;
    ScreenReader::Speak(buf, true);
    Log::Menu("[TutorialTTS] key=%d: %s", key, buf);
}
