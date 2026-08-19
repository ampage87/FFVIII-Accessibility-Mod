// menu_tutorial_model.inl -- v0.26.0 (#85)
//
// The Tutorial menu and the SeeD written exam: ANNOUNCEMENT LOGIC ONLY. Pure
// functions of a TutorialView / SeedExamView -- no Win32, no SEH, no absolute
// memory. Same split as the Magic, Junction, Card and Config models, driven
// offline by tests/menu_sim.cpp and tests/menu_tutorial_compile.cpp.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE, before menu_tts_tutorial.inl.
//
// ---------------------------------------------------------------------------
// WHERE THIS COMES FROM
//
// **Two modules, not one.** The Tutorial menu is main-menu dispatch index 20
// (creator 0x004C9B70, update 0x004C9CB0, draw 0x004CAE10, 34 states, jump
// table 0x004CAC0C). Choosing TEST or Review does not change its state -- it
// PUSHES a second module, the exam (dispatch 23, creator 0x004D4960, update
// 0x004D4D30, draw 0x004D58A0, 28 states, jump table 0x004D5828) and then sits
// in its own state 18 waiting for that module to pop. So the poll has to find
// whichever of the two is live, and prefer the exam when both are.
//
// Full write-up in docs/TUTORIAL_MENU_FINDINGS.md.
// ---------------------------------------------------------------------------

#ifndef MENU_TUTORIAL_MODEL_INCLUDED
#define MENU_TUTORIAL_MODEL_INCLUDED

// ===========================================================================
// States the player can SIT STILL IN.
//
// Same rule that found the junction grid and misled the card album: a state the
// machine passes THROUGH is not one the player is IN, and "does it read input"
// is not the test -- the Tutorial's states 9 and 11 read Left/Right mid-slide to
// queue another page flip, exactly like the Card album's 7 and 9.
// ===========================================================================
static const int TUT_STATE_LIST      = 4;    // the seven-row Tutorial menu
static const int TUT_STATE_TESTPICK  = 7;    // choose which past test to review

// The exam. 5, 8 and 23 share one handler (0x004D4F43) -- a generic "message
// window, wait for Confirm" -- and 11, 13, 16 are the same shape with a choice.
static const int SEED_STATE_QUESTION = 21;   // 0x004D5534: the answer input
static const int SEED_STATE_MSG_A    = 5;
static const int SEED_STATE_MSG_B    = 8;
static const int SEED_STATE_MSG_C    = 11;
static const int SEED_STATE_MSG_D    = 13;
static const int SEED_STATE_MSG_E    = 16;
static const int SEED_STATE_RESULT   = 23;

static bool SeedStateIsMessage(int s)
{
    return s == SEED_STATE_MSG_A || s == SEED_STATE_MSG_B || s == SEED_STATE_MSG_C
        || s == SEED_STATE_MSG_D || s == SEED_STATE_MSG_E || s == SEED_STATE_RESULT;
}

static const int SEED_QUESTIONS_PER_TEST = 10;
static const int SEED_TEST_COUNT         = 30;

// ===========================================================================
// The glyph table.
//
// A text byte is `glyph index + 0x20`, so this is indexed by `byte - 0x20`. It
// is the sysfnt grid, transliterated to plain ASCII on purpose: SAPI is being
// handed these strings and an accented capital adds nothing a listener can hear.
// ===========================================================================
static const char* const TUT_GLYPH[] = {
    /* 0x20 */ " ", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "%", "/", ":", "!", "?",
    /* 0x30 */ "...", "+", "-", "=", "*", "&", "\"", "\"", "(", ")", "-", ".", ",", "~", "\"", "\"",
    /* 0x40 */ "'", "#", "$", "'", "_", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
    /* 0x50 */ "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "a",
    /* 0x60 */ "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
    /* 0x70 */ "r", "s", "t", "u", "v", "w", "x", "y", "z", "A", "A", "A", "A", "C", "E", "E",
    /* 0x80 */ "E", "E", "I", "I", "I", "I", "N", "O", "O", "O", "O", "U", "U", "U", "U", "O",
    /* 0x90 */ "a", "a", "a", "a", "c", "e", "e", "e", "e", "i", "i", "i", "i", "n", "o", "o",
    /* 0xA0 */ "o", "o", "u", "u", "u", "u", "o", "ss", "!", "?", "\"", "\"", "-", "-", "-", "-",
    // 0xB5 is the only byte above 0xAF that occurs anywhere in the exam corpus --
    // three times, and all three sit exactly where a pause belongs: "won't go any
    // higher<B5> will you still take the test", "GFs have levels<B5> the higher",
    // "must be set<B5> otherwise". The GLYPH is not established (it is not the
    // 0x3C comma, and the shapes live in the font, not the exe), but dropping it
    // runs two clauses together in all three. A comma is the reading that is
    // right whichever punctuation mark it actually draws.
    /* 0xB0 */ "", "", "", "", "", ", ", "", "", "", "", "", "", "", "", "", "",
};
static const int TUT_GLYPH_COUNT = (int)(sizeof(TUT_GLYPH) / sizeof(TUT_GLYPH[0]));

// The two-letter compression pairs, 0xE8..0xFF -- the same table the mod's
// shared decoder already carries for enemy and item text.
static const char* const TUT_DIGRAM[24] = {
    "in", "e ", "ne", "to", "re", "HP", "l ", "ll",
    "GF", "nt", "il", "o ", "ef", "on", " w", " r",
    "wi", "fi", "EC", "s ", "ar", "FE", " S", "ag"
};

// ===========================================================================
// The symbols. **This is the part Aaron asked for.**
//
// *"In particular we need to make the SeeD Exam Quiz and its questions
// accessible - including the symbols it sometimes displays in various
// questions."*
//
// Control code 0x05 takes one parameter byte and draws a sprite:
//   0x20..0x2F  a GAME FUNCTION, remapped through the player's own button map
//               (0x004A2DF0) before the sprite is chosen -- so the picture on
//               screen changes when the controls change, and the only stable
//               thing to say is what the button DOES.
//   0x30..0x3F  a fixed physical button, no remap.
//   0x40+       an inline icon, sprite = u16[0x00B86D84 + param*2].
//
// Twenty-nine of the three hundred live questions contain one.
// ===========================================================================

// 0x05 0x40.. -- the ability icons. **These names are not mine.** They are read
// off the game's own Icon Explanation page (mngrp section 89, string 54), which
// labels each sprite in text, and every one of them is independently confirmed
// by the stored answer key of the question it appears in.
//
// **Naming them is a deliberate choice with a cost.** Six questions are of the
// form "<icon> signifies Junction Ability", and speaking the icon's true name
// turns a recognition test into a string comparison. The alternative is worse:
// a test is only passed by answering ALL TEN questions correctly, so an
// unnamed icon does not make those tests harder, it makes six of the thirty
// **unpassable except by luck**. There is no reading of "accessible" where that
// is the better outcome.
// v0.27.0: the element and status icons are named on the same authority -- the
// Icon Explanation pages themselves, mngrp section 89 strings 43, 46 and 47,
// each of which draws the sprite and then writes its name beside it. Reading
// those pages aloud is therefore slightly circular ("Fire symbol, Fire") and
// that is exactly right: the page IS the legend, and it is what teaches the
// player what the mod will call the same sprite when it turns up elsewhere.
//
// 0x41 and 0x42 are named WITHOUT an article on purpose. Each occurs in exactly
// one sentence in the whole corpus and both sentences supply their own: "The
// {41} indicates that the magic is junctioned", "{42} may appear next to
// Attack". The ability icons keep theirs because their sentences do not.
static const char* TutIconName(unsigned char p)
{
    switch (p) {
        case 0x41: return "junction marker";
        case 0x42: return "Limit Break marker";
        case 0x43: return "the damage-absorbed symbol";
        case 0x45: return "the Junction Ability icon";
        case 0x46: return "the Command Ability icon";
        case 0x48: return "the Character Ability icon";
        case 0x49: return "the Party Ability icon";
        case 0x4A: return "the GF Ability icon";
        case 0x4B: return "the Menu Ability icon";
        // Elements, from section 89 string 43.
        case 0x5D: return "Fire symbol";
        case 0x5E: return "Ice symbol";
        case 0x5F: return "Thunder symbol";
        case 0x60: return "Earth symbol";
        case 0x61: return "Poison symbol";
        case 0x62: return "Wind symbol";
        case 0x63: return "Water symbol";
        case 0x64: return "Holy symbol";
        // Statuses, from section 89 strings 46 and 47.
        case 0x65: return "Death symbol";
        case 0x66: return "Poison symbol";
        case 0x67: return "Petrify symbol";
        case 0x68: return "Darkness symbol";
        case 0x69: return "Silence symbol";
        case 0x6A: return "Berserk symbol";
        case 0x6B: return "Zombie symbol";
        case 0x6C: return "Sleep symbol";
        case 0x6D: return "Slow symbol";
        case 0x6E: return "Stop symbol";
        case 0x6F: return "Curse symbol";
        case 0x70: return "Confuse symbol";
        case 0x71: return "Drain symbol";
        default:   return 0;
    }
}

// 0x05 0x20..0x2F -- a game function whose button the player may have remapped.
//
// The names are the game's OWN function names, taken from the Config Customize
// screen's row table (0x00B88A10) and its battle-page labels, which is why they
// match what menu_config_model.inl already says. Speaking the function rather
// than a button shape is not a compromise: the shape is a sprite in icon.sp1
// that changes with the player's own map, so a fixed shape name would be wrong
// as often as it was right.
static const char* TutActionName(unsigned char p)
{
    switch (p) {
        case 0x20: return "the first Escape button";
        case 0x21: return "the second Escape button";
        case 0x22: return "the Change Select Window button";
        case 0x23: return "the Trigger button";
        case 0x24: return "the Cancel button";
        case 0x25: return "the Change Character button";
        case 0x26: return "the Confirm button";
        case 0x27: return "the View Status button";
        // The four directions. 0x2C..0x2F are functions 12..15, and the ordering
        // is corroborated by the game's own "{2C}{2E}{2F}{2D} cursor" line.
        case 0x2C: return "Up";
        case 0x2D: return "Right";
        case 0x2E: return "Down";
        case 0x2F: return "Left";
        default:   return 0;
    }
}

// 0x05 0x30..0x3F -- a fixed physical button, never remapped. Only 0x38 occurs
// in exam text.
//
// **0x38 is deliberately not given a name.** It is one of the two buttons the
// Customize screen refuses to rebind -- Start and Select -- and which of the two
// it is cannot be established from the exe: the evidence is an inference from
// one help string, and the sprite itself lives in icon.sp1. The one question it
// appears in ("Press X to hide battle commands temporarily", answer YES) reads
// correctly without the name, so a guess would buy nothing and could mislead.
static const char* TutFixedButtonName(unsigned char p)
{
    // 0x38 and 0x3B are the two buttons the Config Customize screen refuses to
    // rebind -- Start and Select -- and **which is which is not established.**
    // Both of their sentences say what the button does ("to hide battle commands
    // temporarily", "to see a help message"), so the sentence still carries its
    // meaning without a name, and a guess could send the player pressing the
    // wrong one.
    if (p == 0x38 || p == 0x3B) return "a button";
    return 0;
}

// ===========================================================================
// The expander.
//
// Walks raw FF8 menu text and produces something speakable. Control codes, from
// the renderers at 0x004D6DA0 (tutorial pages) and 0x004D4AF0 (the exam):
//
//   0x00       end of string
//   0x01       end of page
//   0x02       line break        -> ". " so SAPI takes a breath at the line
//   0x03 nn    character name    -> the player's own name for that character
//   0x05 nn    button or icon    -> see above
//   0x06 nn    text colour       -> dropped; a screen reader has no colour
//   0x0A nn    variable / flag   -> a number, or a visibility condition
//   0x0B nn    a selectable line -> in the exam, answer slot nn - 0x20
//   0x0C nn    GF name           -> the player's own name for that GF
//
// Names come in through the view, already read out of the savemap, because this
// file is not allowed to touch memory.
// ===========================================================================

// What a caller wants to know about the text it just expanded.
struct TutTextInfo
{
    int  choiceCount;       // how many 0x0B answer slots were seen
    int  firstChoiceIndex;  // slot id of the first, or -1
    // The words on the answer line, in cursor order.
    //
    // **They are not always YES then NO.** Section 95 string 7 is the "Really?"
    // confirmation and it lists **NO first**; string 5 offers "GO BACK" and the
    // result screens offer "END". Hard-coding Yes/No would have told the player
    // the opposite of what the cursor was on, on the one screen whose entire
    // job is to double-check them.
    int  labelCount;
    char labels[4][20];
    // Set when the column rule fired -- i.e. this line was a label/value row of
    // a table rather than a sentence. The Information browser uses it to put a
    // stop between rows so "Walked 109751 Battles 41" cannot run together.
    bool columns;
};

// Split the answer line into labels. FF8 separates them with a RUN of spaces and
// nothing else -- the 0x0B markers never reach the drawn buffer -- so a run of
// two or more is the delimiter and a single space is part of the label. That is
// what keeps "GO BACK" one answer and "YES     NO" two.
static void TutSplitLabels(const char* src, TutTextInfo* info)
{
    if (!info) return;
    info->labelCount = 0;
    if (!src) return;
    const char* p = src;
    while (*p && info->labelCount < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* start = p;
        const char* end   = p;
        while (*p) {
            if (p[0] == ' ' && p[1] == ' ') break;
            if (*p != ' ') end = p + 1;
            p++;
        }
        size_t len = (size_t)(end - start);
        if (len > sizeof(info->labels[0]) - 1) len = sizeof(info->labels[0]) - 1;
        char* dst = info->labels[info->labelCount];
        memcpy(dst, start, len);
        dst[len] = '\0';
        // **Title-case it.** The screen draws these in capitals -- YES, NO, END,
        // GO BACK -- and a synthesiser handed a short all-capital token is liable
        // to spell it out letter by letter. "N. O." in answer to a question the
        // player is trying to answer is worse than useless. The WORD is still the
        // screen's own; only the case is the mod's.
        for (size_t i = 0; i < len; i++) {
            char c = dst[i];
            if (i == 0) { if (c >= 'a' && c <= 'z') dst[i] = (char)(c - 32); }
            else        { if (c >= 'A' && c <= 'Z') dst[i] = (char)(c + 32); }
        }
        info->labelCount++;
    }
}

static void TutAppend(char* out, size_t n, const char* s)
{
    if (!out || !s) return;
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}
static void TutAppendInt(char* out, size_t n, long v)
{
    char t[24]; snprintf(t, sizeof(t), "%ld", v);
    TutAppend(out, n, t);
}

// An expanded word -- a name, a GF, a symbol -- always padded on BOTH sides.
// Real question text puts two symbols back to back ("Hold down <Trigger><R2>
// simultaneously", test 23 question 2) and without the padding they fuse into
// "the Trigger buttonthe second Escape button". TutTidy takes the extra spaces
// back out again, including the one that would otherwise land before "'s".
static void TutAppendWord(char* out, size_t n, const char* s)
{
    TutAppend(out, n, " ");
    TutAppend(out, n, s);
    TutAppend(out, n, " ");
}

// Collapse runs of spaces and stray punctuation the line breaks leave behind.
// FF8 pads its text to fixed column widths, so a naive expansion says "Draw
// command      extracts" and SAPI reads the gap as a pause mid-clause.
static void TutTidy(char* s)
{
    if (!s) return;
    char* w = s;
    for (char* r = s; *r; r++) {
        if (*r == ' ' && (w == s || w[-1] == ' ')) continue;
        *w++ = *r;
    }
    while (w > s && w[-1] == ' ') w--;
    *w = '\0';
    // A space in front of punctuation. FF8 pads every line out to a fixed column
    // width, so the stop this expander puts at a line break lands after the
    // padding and SAPI reads "GF stands for . Garden Fighter" with a beat in the
    // wrong place. The apostrophe is in this list for a different reason: every
    // expanded name is padded with spaces on both sides (see TutAppendWord), and
    // without it the real question text reads "Squall 's weapon".
    {
        char* q = s;
        for (char* p = s; *p; p++) {
            // The apostrophe case is narrower than it looks. It exists because
            // every expanded name is padded with spaces, so "{03}30's" arrives as
            // " Squall 's" -- but the Card Rules page legitimately writes
            // `have 'A' value`, and stripping there gives "have'A'". A
            // POSSESSIVE is followed by a lower-case letter; an opening quote is
            // not. That is the whole discriminator.
            if (*p == ' ' && (p[1] == '.' || p[1] == ',' || p[1] == '!' || p[1] == '?'))
                continue;
            if (*p == ' ' && p[1] == '\'' && p[2] >= 'a' && p[2] <= 'z')
                continue;
            *q++ = *p;
        }
        *q = '\0';
    }
    // A trailing run of stops down to one. Nothing in the corpus needs more,
    // and "the Gauntlet.." reads as a stutter.
    {
        size_t l = strlen(s);
        while (l > 1 && s[l-1] == '.' && s[l-2] == '.') l--;
        s[l] = '\0';
    }
    // ". ." and ".." come from a line break landing next to real punctuation.
    for (char* p = s; *p; ) {
        if (p[0] == '.' && p[1] == ' ' && p[2] == '.') { memmove(p, p + 2, strlen(p + 2) + 1); continue; }
        if (p[0] == '.' && p[1] == '.' && p[2] != '.') { memmove(p, p + 1, strlen(p + 1) + 1); continue; }
        p++;
    }
}

// `names` is 8 character names, `gfNames` is 16 GF names; either may be null,
// in which case the code degrades to a neutral word rather than a wrong one.
// `cutAtLine` is the line the ANSWER LABELS start on, or -1 for "no labels".
//
// **This is the fix for the exam's message windows.** The text the game draws is
// the pre-processed buffer at 0x01D7DAB8, and 0x004D4A80 does NOT copy the 0x0B
// answer-slot markers into it -- it diverts them to a position array and copies
// the label letters straight through. So "YES     NO" and "END" arrive as
// ordinary words on the end of the message with nothing to cut on. What the
// position array DOES give is each choice's pen y, and the pen advances by
// exactly 0x10 per line break (0x004D4CD3), so `y / 0x10` is the line the labels
// begin on and everything from there down is a label.
static void TutExpand(const unsigned char* txt, int len,
                      const char* const* names, const char* const* gfNames,
                      char* out, size_t n, TutTextInfo* info, int cutAtLine)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (info) { info->choiceCount = 0; info->firstChoiceIndex = -1; info->labelCount = 0;
                info->columns = false; }
    if (!txt || len <= 0) return;

    bool suppress = false;   // set at the first answer slot, or at the cut line
    int  line      = 0;
    char labelBuf[128];
    labelBuf[0] = '\0';

    for (int i = 0; i < len; i++) {
        const unsigned char b = txt[i];
        if (b == 0x00 || b == 0x01) break;

        if (suppress) {
            // Still parsing, no longer speaking the QUESTION -- but the words
            // down here are the answer labels, and the mod has to name them.
            if (b == 0x02) { line++; TutAppend(labelBuf, sizeof(labelBuf), "  "); continue; }
            if (b >= 0xE8) { TutAppend(labelBuf, sizeof(labelBuf), TUT_DIGRAM[b - 0xE8]); continue; }
            if (b >= 0x20) {
                const int li = (int)b - 0x20;
                if (li >= 0 && li < TUT_GLYPH_COUNT)
                    TutAppend(labelBuf, sizeof(labelBuf), TUT_GLYPH[li]);
                continue;
            }
            if (b == 0x03 || b == 0x05 || b == 0x06 || b == 0x0A ||
                b == 0x0C || b == 0x0D || b == 0x0E || b == 0x0F) { i++; continue; }
            if (b == 0x0B) {
                const unsigned char p = (i + 1 < len) ? txt[++i] : 0;
                (void)p;
                if (info) info->choiceCount++;
            }
            continue;
        }

        // **A line break is a WRAP, not a sentence end.** FF8 breaks its text
        // at a fixed column, so a stop here splits real sentences in half:
        // "Squall's gunblade causes more damage. by pressing the first Escape
        // button" is one of the game's own questions read wrong. A space is
        // right in every case, because a genuine sentence end already carries
        // its own full stop and a blank padding line collapses away in TutTidy.
        if (b == 0x02) {
            line++;
            if (cutAtLine >= 0 && line >= cutAtLine) { suppress = true; continue; }
            // **A space, always.** v0.27.0 briefly tried "short line means list
            // item, long line means wrap" so the Icon Explanation stats page
            // ("Hit Points", "Strength", "Vitality", one per line) would read as
            // a list instead of one run-on breath. Against the real corpus it put
            // commas inside sentences -- "Same Wall uses Battle, Area wall", "Wall
            // is assumed to have, 'A' value" -- because a wrapped line can be as
            // short as 21 characters and a list item as long as 16. There is no
            // threshold that separates them.
            //
            // **A wrong comma is a lie about the text; a missing one is only
            // flat.** The list pages stay flat. When the Information browser
            // lands, its lists are LINKS, and those are enumerated from the
            // position array rather than guessed at from line lengths.
            TutAppend(out, n, " ");
            continue;
        }

        if (b == 0x03) {                       // character name
            const unsigned char p = (i + 1 < len) ? txt[++i] : 0;
            const int id = (int)p - 0x30;
            if (names && id >= 0 && id < 8 && names[id] && names[id][0])
                TutAppendWord(out, n, names[id]);
            else
                TutAppendWord(out, n, "the character");
            continue;
        }
        if (b == 0x0C) {                       // GF name
            const unsigned char p = (i + 1 < len) ? txt[++i] : 0;
            const int id = (int)p - 0x60;
            if (gfNames && id >= 0 && id < 16 && gfNames[id] && gfNames[id][0])
                TutAppendWord(out, n, gfNames[id]);
            else
                TutAppendWord(out, n, "the GF");
            continue;
        }
        if (b == 0x05) {                       // button or icon
            const unsigned char p = (i + 1 < len) ? txt[++i] : 0;
            const char* s = TutIconName(p);
            if (!s) s = TutActionName(p);
            if (!s) s = TutFixedButtonName(p);
            // An unrecognised sprite is spoken as "a symbol" rather than
            // dropped: the player must know something was there, or a question
            // that turns on it reads as a sentence with a hole in it.
            TutAppendWord(out, n, s ? s : "a symbol");
            continue;
        }
        if (b == 0x0B) {                       // a selectable answer slot
            const unsigned char p = (i + 1 < len) ? txt[++i] : 0;
            if (info) {
                if (info->choiceCount == 0) info->firstChoiceIndex = (int)p - 0x20;
                info->choiceCount++;
            }
            // **Everything from here on is the answer LABELS, not the question.**
            // Real exam text ends "...the Gauntlet.\n\n  <slot0>YES     <slot1>NO",
            // so emitting past this point makes every question read
            // "...the Gauntlet. YES NO" before the mod then says "Answer Yes".
            // The scan continues so the count is still right.
            suppress = true;
            continue;
        }
        if (b == 0x06 || b == 0x0A || b == 0x0D || b == 0x0E || b == 0x0F) {
            if (i + 1 < len) i++;              // parameterised, nothing to say
            continue;
        }
        if (b < 0x20) continue;                // the width-only codes 0x10..0x18

        // **A run of spaces inside a line is a COLUMN, not a gap.**
        //
        // FF8 pads label/value tables out with literal spaces to line the
        // columns up: the Battle Report draws "Walked      109751" and the magic
        // pages draw "Target   Single". Joined as spaces those read as
        // "Walked 109751 Battles 41 Won 35 Escaped 6" -- four labels and four
        // numbers with nothing saying which belongs to which.
        //
        // The guard is what makes this safe where the v0.27.0 line-length
        // heuristic was not: the run must be REAL SOURCE SPACES, two or more,
        // and the character already emitted before them must be a letter or a
        // digit. Sentence spacing ("...disabled in battle.  Death is KO...")
        // follows a full stop and is left alone; an indented continuation
        // follows the space a line break just emitted and is left alone too.
        if (b == 0x20 && i + 1 < len && txt[i + 1] == 0x20) {
            while (i + 1 < len && txt[i + 1] == 0x20) i++;
            // **The run must have text after it on the same line.** 12 of the
            // 2,926 line breaks in the Information corpus are preceded by
            // trailing padding, and a comma there lands at a WRAP -- which is
            // precisely the failure that killed the v0.27.0 heuristic. A run
            // that ends the line is padding; a run with a word after it is a
            // column.
            const unsigned char nxt = (i + 1 < len) ? txt[i + 1] : 0x00;
            if (nxt == 0x00 || nxt == 0x01 || nxt == 0x02) { TutAppend(out, n, " "); continue; }
            const size_t l = strlen(out);
            const char prev = l ? out[l - 1] : '\0';
            const bool word = (prev >= 'a' && prev <= 'z') ||
                              (prev >= 'A' && prev <= 'Z') ||
                              (prev >= '0' && prev <= '9') || prev == '%';
            if (word && info) info->columns = true;
            TutAppend(out, n, word ? ", " : " ");
            continue;
        }

        if (b >= 0xE8) { TutAppend(out, n, TUT_DIGRAM[b - 0xE8]); continue; }

        const int gi = (int)b - 0x20;
        if (gi >= 0 && gi < TUT_GLYPH_COUNT) TutAppend(out, n, TUT_GLYPH[gi]);
        // Anything else is a glyph this build has no letter for. Dropping it is
        // right: it is an accent or a JP-only form, never a whole word.
    }
    TutTidy(out);
    if (labelBuf[0]) TutSplitLabels(labelBuf, info);
}

// ===========================================================================
// The Tutorial menu's seven rows (table 0x00B88340, stride 4, 0xFF-terminated).
//
// Titles and descriptions are the game's own, decoded from mngrp: rows with a
// non-zero kind byte read section 2 bank 13, rows with kind 0 read section 0
// bank 0 through 0x004C25B0. They are fixed English strings, so they live here
// as data rather than being chased through two heaps every frame -- the same
// call the Config screen's row labels make.
// ===========================================================================
static const int TUT_ROW_COUNT = 7;

struct TutorialRow { const char* title; const char* help; };

static const TutorialRow TUT_ROWS[TUT_ROW_COUNT] = {
    { "Battle Operation",  "Battle Explanation" },
    { "Online Help",       "Explanation of Various Features" },
    { "Card Game Rules",   "Card Game Explanation" },
    { "Test",              "Take Written Test to raise SeeD rank" },
    { "Review",            "Review SeeD Written Test" },
    { "Icon Explanation",  "Explanation About Icons" },
    { "Information",       "Final Fantasy 8 Info Corner" },
};

static const int TUT_ROW_TEST   = 3;
static const int TUT_ROW_REVIEW = 4;

struct TutorialView
{
    unsigned short state;
    int  cursor;        // module +0x34, row 0..6
    int  testPick;      // module +0x32, which past test to review
    int  testsPassed;   // 0x01CFE98B, 0..30 -- the ONLY record of exam progress
    int  seedRank;      // 0x004C3090(), 1..31, or -1 when not yet a SeeD

    // v0.27.0: the Online Help panel, which lives in this same module.
    int  helpCursor;     // module +0x35
    int  helpCount;      // module +0x36, grows as the story unlocks topics
    int  helpDescriptor; // module[+0x39 + helpCursor], an index into TUT_HELP
    const char* const* charNames;   // for the three rows named after a character
};

// The row under the cursor, plus whether it can actually be entered. Both gates
// are the game's own: TEST needs the rank getter to return >= 0, Review needs at
// least one test already passed. A row that does nothing when confirmed has to
// say so, or the player is left pressing Confirm at a screen that never answers.
static void TutAnnounceRow(const TutorialView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int r = v.cursor;
    if (r < 0 || r >= TUT_ROW_COUNT) return;

    TutAppend(out, n, TUT_ROWS[r].title);
    TutAppend(out, n, ". ");
    TutAppend(out, n, TUT_ROWS[r].help);

    if (r == TUT_ROW_TEST) {
        if (v.seedRank < 0) {
            TutAppend(out, n, ". Not available until you are a SeeD");
        } else if (v.testsPassed >= SEED_TEST_COUNT) {
            TutAppend(out, n, ". All 30 tests passed");
        } else {
            TutAppend(out, n, ". Next is test ");
            TutAppendInt(out, n, v.testsPassed + 1);
            TutAppend(out, n, " of 30");
        }
    } else if (r == TUT_ROW_REVIEW) {
        if (v.testsPassed <= 0) {
            TutAppend(out, n, ". Not available until you have passed a test");
        } else {
            TutAppend(out, n, ". ");
            TutAppendInt(out, n, v.testsPassed);
            TutAppend(out, n, v.testsPassed == 1 ? " test to review" : " tests to review");
        }
    }
}

static void TutAnnouncePosition(const TutorialView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.cursor < 0 || v.cursor >= TUT_ROW_COUNT) return;
    TutAppend(out, n, "Item ");
    TutAppendInt(out, n, v.cursor + 1);
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, TUT_ROW_COUNT);
}

// The SeeD standing, on demand. The rank is what the whole exam is FOR -- it
// sets the salary -- and no screen in the menu states it plainly.
static void TutAnnounceStanding(const TutorialView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.seedRank < 0) { TutAppend(out, n, "You are not a SeeD yet"); return; }
    TutAppend(out, n, "SeeD rank ");
    TutAppendInt(out, n, v.seedRank);
    TutAppend(out, n, " of 31. ");
    TutAppendInt(out, n, v.testsPassed);
    TutAppend(out, n, " of 30 written tests passed");
}

// The review picker (state 7). The cursor is a flat index over ten rows a page
// and the list is exactly as long as the number of tests already passed.
static void TutAnnounceTestPick(const TutorialView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int sel = v.testPick;
    if (sel < 0) return;
    TutAppend(out, n, "Test ");
    TutAppendInt(out, n, sel + 1);
    if (sel >= v.testsPassed) {
        // The game beeps and refuses rather than explaining. Say why.
        TutAppend(out, n, ", not yet passed");
        return;
    }
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, v.testsPassed);
}

// ===========================================================================
// The exam.
//
// Ten questions, and **every one of them must be right** (0x004D55E4 compares
// the score against 0x0A). That single fact drives the wording: there is no
// partial credit to report and no value in a running score, so the announcement
// spends its words on the question and the two answers instead.
// ===========================================================================
struct SeedExamView
{
    unsigned short state;
    int  testIndex;      // module +0x2D, 0-based; level = +1
    int  questionIndex;  // module +0x2E, 0..9
    int  choice;         // module +0x2F, 0 = YES, 1 = NO
    int  choiceCount;    // 0x01D7EAB8
    bool reviewMode;     // module +0x2C

    // The question, already expanded by the memory layer (which is the only
    // thing allowed to read the savemap for character and GF names).
    char text[512];
    // The message window's text, for the offer / gate / pass / fail screens.
    char message[512];
    // The answer words, in cursor order, lifted off the screen's own answer line.
    int  labelCount;
    char labels[4][20];
};

// **The question text already says "Question 3".** Every stored question opens
// with its own number and then two blank lines, so speaking the position and
// then the text gives "Question 3 of 10. Question 3. If you receive..." -- the
// number twice, which on a screen the player is trying to think about is worse
// than saying it once. The mod's own label is the one that survives, because it
// carries "of 10" and the game's does not.
static void SeedAppendStem(const SeedExamView& v, char* out, size_t n)
{
    const char* p = v.text;
    if (strncmp(p, "Question ", 9) == 0) {
        const char* q = p + 9;
        while (*q >= '0' && *q <= '9') q++;
        if (q > p + 9) {                       // only if digits actually followed
            while (*q == '.' || *q == ',' || *q == ' ') q++;
            if (*q) p = q;
        }
    }
    TutAppend(out, n, p);
}

// The word for the answer the cursor is on.
//
// **Read off the screen, not assumed.** The exam's answers are usually YES then
// NO, but section 95 string 7 -- the "Really?" confirmation -- lists **NO
// first**, and other screens offer END or GO BACK. Hard-coding Yes/No would have
// named the opposite of what the cursor was on, on the one screen whose entire
// job is to double-check the player. The Yes/No fallback is only for the case
// where the labels could not be read at all.
static const char* SeedChoiceWord(const SeedExamView& v, int c)
{
    if (c >= 0 && c < v.labelCount && c < 4 && v.labels[c][0]) return v.labels[c];
    if (c == 0) return "Yes";
    if (c == 1) return "No";
    return "";
}

// The whole question: where you are, what is being asked, and what is currently
// selected. Spoken on arriving at each new question.
static void SeedAnnounceQuestion(const SeedExamView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    TutAppend(out, n, "Question ");
    TutAppendInt(out, n, v.questionIndex + 1);
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, SEED_QUESTIONS_PER_TEST);
    TutAppend(out, n, ". ");
    SeedAppendStem(v, out, n);
    TutAppend(out, n, " Answer ");
    TutAppend(out, n, SeedChoiceWord(v, v.choice));
}

// Moving between Yes and No says only the word. The question has just been read
// and re-reading it on every press would make the choice unusable.
static void SeedAnnounceChoice(const SeedExamView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    TutAppend(out, n, SeedChoiceWord(v, v.choice));
}

// On demand: the question again, without the position preamble.
static void SeedAnnounceText(const SeedExamView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    SeedAppendStem(v, out, n);
}

// On demand: where you are. **The running score is deliberately absent.** The
// game never shows it, and a sighted player cannot know whether the answer they
// just gave was right -- reporting it would hand the blind player information
// the screen does not contain, which is a different thing from access to it.
static void SeedAnnouncePosition(const SeedExamView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    TutAppend(out, n, "Question ");
    TutAppendInt(out, n, v.questionIndex + 1);
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, SEED_QUESTIONS_PER_TEST);
    TutAppend(out, n, ", test ");
    TutAppendInt(out, n, v.testIndex + 1);
    if (v.reviewMode) TutAppend(out, n, ", review");
    TutAppend(out, n, ". All ten must be correct to pass");
}

// The message screens: the offer, the gates, the pass and fail results. The
// module keeps the drawn string at +0x20 and the memory layer expands it, so
// this is the game's own wording with its numbers already filled in.
static void SeedAnnounceMessage(const SeedExamView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    TutAppend(out, n, v.message);
    if (v.choiceCount >= 2) {
        TutAppend(out, n, ". ");
        TutAppend(out, n, SeedChoiceWord(v, v.choice));
    }
}

// ===========================================================================
// v0.27.0 (#86): the three magazine screens.
//
// **Battle Operation, Card Game Rules and Icon Explanation are ONE module.**
// Their three creators (0x004C8FF0 / 0x004C9820 / 0x004C9890) are identical --
// all three call 0x004BE540(0x004C9060, 0x004C9330) -- and they differ only in
// what the Tutorial wrote into 0x01D7D3A5/A6 first. So they cannot be told
// apart by update fn; the record range is what says which one you are in.
//
// There is **no cursor and nothing selectable**. Left and Right turn the page,
// Confirm turns it and then leaves past the last one, Cancel leaves. State 9 is
// the only interactive state and, unlike the Card album, no slide state here
// samples input at all.
// ===========================================================================
static const int MAG_STATE_PAGE = 9;

// Which of the three you are in, from the record range the Tutorial preloaded.
// (mtmag.bin: Battle Operation 43..50, Card Game Rules 51..63, Icon Explanation
// 64..67. Records 0..42 belong to the field magazines -- Weapons Monthly, Pet
// Pals and the rest -- which share this viewer but are not reached from here.)
static const char* MagTopicName(int first)
{
    if (first == 43) return "Battle Operation";
    if (first == 51) return "Card Game Rules";
    if (first == 64) return "Icon Explanation";
    return 0;
}

struct MagazineView
{
    unsigned short state;
    int  record;      // module +0x28, the page ON SCREEN
    int  first;       // 0x01D7D3A5
    int  last;        // 0x01D7D3A6
    char text[1400];  // the page's text blocks, already expanded and joined
};

// The page, spoken whole.
//
// The stored text already carries its own heading and its own counter -- "Status
// Window", "  Battle Tutorial 1/8", then the body -- so the mod adds nothing but
// the topic name, and only on the first page, where the player has just arrived
// and has no other way to know which of the three magazines opened.
static void MagAnnouncePage(const MagazineView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int page  = v.record - v.first + 1;
    const int pages = v.last - v.first + 1;
    if (page == 1) {
        const char* topic = MagTopicName(v.first);
        if (topic) { TutAppend(out, n, topic); TutAppend(out, n, ". "); }
    }
    TutAppend(out, n, v.text);
    // The last page is where Confirm stops turning pages and leaves instead, and
    // nothing on screen says so.
    if (pages > 0 && page == pages)
        TutAppend(out, n, " Last page. Confirm or Cancel to leave");
}

// On demand: where you are, and how to move. The page counter is inside the
// text, but a player who has been listening to eight screens of prose should not
// have to replay one to find it.
static void MagAnnouncePosition(const MagazineView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int page  = v.record - v.first + 1;
    const int pages = v.last - v.first + 1;
    const char* topic = MagTopicName(v.first);
    if (topic) { TutAppend(out, n, topic); TutAppend(out, n, ", "); }
    TutAppend(out, n, "page ");
    TutAppendInt(out, n, page);
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, pages);
    TutAppend(out, n, ". Left and right turn the page, Cancel leaves");
}

// ===========================================================================
// v0.27.0 (#86): Online Help.
//
// **This one is not a module at all.** Row 1's action byte is 0xFF, which maps
// to a handler that just sets state 24 -- so the list lives inside the Tutorial
// module itself, as a second panel beside the seven-row list, with its own
// cursor at +0x35 and its own length at +0x36.
//
// Its rows are filtered by story progress: each descriptor carries a flag id and
// the row exists only if 0x004AD1D0(flag) is set, so the list grows as the game
// does. The module writes the surviving rows' descriptor indices into +0x39..,
// which is what this reads -- deriving the filter here would mean reproducing a
// savemap bitmap for no gain.
// ===========================================================================
static const int TUT_STATE_HELPLIST = 27;
static const int TUT_HELP_MAX = 9;

// The nine topics, from the descriptor table at 0x00B88360 and bank 13. Three of
// them are named after a party member, and the game substitutes the player's own
// name -- so those carry a character id instead of a fixed word.
struct TutHelpTopic { const char* prefix; int charId; const char* suffix; const char* help; };

static const TutHelpTopic TUT_HELP[TUT_HELP_MAX] = {
    { "GF Junction",         -1, "", "Junctioning a GF and setting commands" },
    { "Magic Junction",      -1, "", "Explanation on junctioning magic" },
    { "Junction to Elements",-1, "", "Explanation of elemental junction" },
    { "Junction of Status",  -1, "", "Explanation of status junction" },
    { "GF Tutorial",         -1, "", "Explanation of GF" },
    { "",                     0, "'s Status Screen", "Explanation of the status screen" },
    { "Zell's Status Screen",-1, "", "Explanation of Zell's status screen" },
    { "",                     4, "'s Status Screen", "Explanation of the status screen" },
    { "Switch",              -1, "", "Explanation of Switch" },
};

// `names` is the same 8-entry table the text expander uses, so a renamed
// character is named the same way here as in a question.
static void TutAnnounceHelpRow(int descriptor, const char* const* names,
                               int row, int rowCount, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (descriptor < 0 || descriptor >= TUT_HELP_MAX) return;
    const TutHelpTopic& t = TUT_HELP[descriptor];

    if (t.charId >= 0) {
        const char* nm = (names && t.charId < 8 && names[t.charId] && names[t.charId][0])
                       ? names[t.charId] : "the character";
        TutAppend(out, n, nm);
    }
    TutAppend(out, n, t.prefix);
    TutAppend(out, n, t.suffix);
    TutAppend(out, n, ". ");
    TutAppend(out, n, t.help);
    if (rowCount > 0) {
        TutAppend(out, n, ". ");
        TutAppendInt(out, n, row + 1);
        TutAppend(out, n, " of ");
        TutAppendInt(out, n, rowCount);
    }
}

// The panel's own heading, spoken on arrival. The list is short and its length
// changes with story progress, so saying how long it is now is worth a clause.
static void TutAnnounceHelpArrival(int rowCount, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    TutAppend(out, n, "Online Help. ");
    TutAppendInt(out, n, rowCount);
    TutAppend(out, n, rowCount == 1 ? " topic" : " topics");
    TutAppend(out, n, ". These open a guided demonstration the mod does not yet "
                      "describe. Cancel goes back");
}

// ===========================================================================
// v0.28.0 (#87): Information -- the nested page browser (menutips, dispatch 21).
//
// 425 records across mngrp sections 128-133, and the largest single body of
// writing in the game's menus: it is the glossary. A record is
// `u16 parent, u16 prevPage, u16 nextPage, u16 size`, then a NUL-terminated
// title, then a NUL-terminated body. Pages link to other pages, sibling pages
// chain left and right, Cancel climbs to the parent, and there is a history
// stack behind it all.
//
// Update 0x004D5F10, 22 states, jump table 0x004D6A5C. **Steady state 7 only**,
// and no slide state samples input.
//
// **The links are enumerated, not guessed.** 0x004D6B20 expands the record into
// 0x01D7EC48 and, exactly like the exam's answer labels, does NOT copy the 0x0B
// markers -- it diverts each to a position array at 0x01D85658 as
// {u16 penX, u16 penY, u16 targetRecord}. The pen advances 0x10 per line break,
// so penY / 0x10 is the link's line. **Checked against all 425 records: not one
// page puts two links on the same line**, so a link's line IS its label, and the
// magazine pages' unsolvable "is this line a list item or a wrap" problem simply
// does not arise here.
// ===========================================================================
static const int TIPS_STATE_PAGE = 7;
static const int TIPS_MAX_LINKS  = 24;   // the busiest real page has 20
static const int TIPS_LABEL_MAX  = 64;

struct TipsView
{
    unsigned short state;
    int  record;       // module +0x28
    int  cursor;       // byte[0x01D83E4C + record] -- saved PER RECORD by the game
    int  linkCount;    // 0x01D85650
    bool hasPrev;      // 0x01D8575A != 0xFFFF
    bool hasNext;      // 0x01D85758 != 0xFFFF
    bool hasParent;    // 0x01D84E4C != 0xFFFF -- false only on the root
    char title[160];
    char body[1600];   // the PROSE lines only; link lines are lifted out below
    char links[TIPS_MAX_LINKS][TIPS_LABEL_MAX];
};

static const char* TipsLink(const TipsView& v, int i)
{
    if (i < 0 || i >= v.linkCount || i >= TIPS_MAX_LINKS) return "";
    return v.links[i];
}

// The page on arrival: what it is called, whatever prose it has, and then how
// many ways out of it there are.
//
// A page is almost always one or the other -- "Select term" is ten links and no
// prose, "Status/About Status" is eight lines of prose and no links -- so this
// reads naturally in both shapes without needing to know which it is.
static void TipsAnnouncePage(const TipsView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.title[0]) { TutAppend(out, n, v.title); TutAppend(out, n, ". "); }
    if (v.body[0])  { TutAppend(out, n, v.body);  TutAppend(out, n, " "); }

    // **An empty page has to say so.** Several pages are nothing but links that
    // the story has not unlocked yet -- "Select name" under Person is blank until
    // you have met somebody -- and the game draws an empty window. Announcing the
    // title and then falling silent is indistinguishable from the mod failing.
    if (v.linkCount <= 0 && v.body[0] == '\0')
        TutAppend(out, n, "Nothing here yet");

    if (v.linkCount > 0) {
        TutAppendInt(out, n, v.linkCount);
        TutAppend(out, n, v.linkCount == 1 ? " topic. " : " topics. ");
        TutAppend(out, n, TipsLink(v, v.cursor));
        TutAppend(out, n, ", ");
        TutAppendInt(out, n, v.cursor + 1);
        TutAppend(out, n, " of ");
        TutAppendInt(out, n, v.linkCount);
    }
    // Sibling pages are reached with Left and Right and nothing on screen says
    // so except a "1/2" buried in the title.
    //
    // The stop is added only if the sentence before it has not already ended --
    // a prose page finishes with its own full stop and would otherwise read
    // "...and Petrify. . More pages".
    if (v.hasNext || v.hasPrev) {
        size_t l = strlen(out);
        while (l > 0 && out[l-1] == ' ') { out[l-1] = '\0'; l--; }
        const char last = l ? out[l-1] : '.';
        if (last != '.' && last != '!' && last != '?') TutAppend(out, n, ".");
        TutAppend(out, n, " More pages: left and right");
    }
}

// Moving between links says the link. The page has just been read and repeating
// it on every press would make a ten-link list unusable.
static void TipsAnnounceLink(const TipsView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.linkCount <= 0) return;
    TutAppend(out, n, TipsLink(v, v.cursor));
    TutAppend(out, n, ", ");
    TutAppendInt(out, n, v.cursor + 1);
    TutAppend(out, n, " of ");
    TutAppendInt(out, n, v.linkCount);
}

// On demand: the page again, prose and all.
static void TipsAnnounceAll(const TipsView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.title[0]) { TutAppend(out, n, v.title); TutAppend(out, n, ". "); }
    TutAppend(out, n, v.body);
}

// On demand: every topic on this page, in order. **This is the thing a sighted
// player gets for free** -- a column of ten headings taken in at a glance -- and
// the one a blind player otherwise has to arrow through to discover.
static void TipsAnnounceLinks(const TipsView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.linkCount <= 0) { TutAppend(out, n, "No topics on this page"); return; }
    TutAppendInt(out, n, v.linkCount);
    TutAppend(out, n, v.linkCount == 1 ? " topic. " : " topics. ");
    for (int i = 0; i < v.linkCount && i < TIPS_MAX_LINKS; i++) {
        if (i) TutAppend(out, n, ", ");
        TutAppend(out, n, v.links[i]);
    }
}

// On demand: where you are and every way out. The back-history button is the
// reason this exists -- the game binds one and **mentions it in no footer, no
// help line and no manual page anywhere in the menu.**
static void TipsAnnounceNav(const TipsView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.linkCount > 0) {
        TutAppend(out, n, "Topic ");
        TutAppendInt(out, n, v.cursor + 1);
        TutAppend(out, n, " of ");
        TutAppendInt(out, n, v.linkCount);
        TutAppend(out, n, ". Confirm opens it. ");
    }
    if (v.hasPrev) TutAppend(out, n, "Left for the previous page. ");
    if (v.hasNext) TutAppend(out, n, "Right for the next page. ");
    TutAppend(out, n, v.hasParent ? "Cancel goes up a level"
                                  : "Cancel leaves Information");
}

#endif // MENU_TUTORIAL_MODEL_INCLUDED
