// battle_window_countdown_model.inl -- A NUMBER, ALONE, IN A BATTLE TEXT BOX
//
// v0.109.0 (#megaflare). Aaron, correcting v0.108.0: *"the countdown timer is
// displayed in a regular FF8 text box, same as spell names when cast or GF
// names when summoned. It is not a sprite that hangs over a character."*
//
// That is the `show_dialog` path, and it is the ONE path battle text takes in
// this mod: the 2026-08-26 log has 39 `[SHOW_DIALOG-TEXT]` lines across the
// session and **zero** `[GETSTR]` lines, so the window poll sees everything and
// the opcode hook sees nothing. "Mega Flare" itself came through it.
//
// SO WHY WAS THE COUNTDOWN NEVER LOGGED? Because both of the gates that could
// have swallowed it sit ABOVE the log line in field_dialog_show_dialog.inl:
//
//     if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return;
//     ...
//     Log::Dialog("... [SHOW_DIALOG-TEXT] ...");
//
// A one-character text ("5") dies on the length half. A text whose whole
// content is a control code the decoder emits nothing for -- 0x0A "special
// value", 0x04 "numeric insert", the two codes FF8 uses to have the ENGINE
// substitute a number at render time (#77) -- dies on the empty half. Either
// way it left no trace, which is why ten seconds of Bahamut's charge are blank
// in every log the mod writes. v0.107.0 fixed the length half and started
// logging it; this file is the other half and the announcement.
//
// THE TWO SHAPES THIS READS
//
// 1. The DECODED text is a bare number -- "5", "12". The decoder already got
//    there and all that is left is to recognise it.
// 2. The RAW bytes are digit glyphs and nothing else. FF8 encodes text as
//    `glyph = byte - 0x20` (TEXT_ENCODING.md S3) and the glyph grid runs
//    ' ' 0 1 2 3 4 5 6 7 8 9 % / : ! ? -- so **'0'..'9' are bytes 0x21..0x2A**
//    and ' ' is 0x20. Reading the raw bytes as well as the decoded string means
//    a number reaches the player even if the decoder mangles the page.
//
// What this deliberately does NOT do is guess at shape 3: a raw string that is
// only `0x04 nn` or `0x0A nn`, where the digits genuinely are not in the
// message and only the engine's expander knows them. If that is what the box
// holds, no amount of reading the raw bytes will produce a number -- and the
// `[BTL-WIN-RAW]` hex line added alongside this will say so in one BAT, which
// is worth more than a guess that might speak the wrong digit at a boss.

static const unsigned char BWC_DIGIT_LO = 0x21;   // '0'
static const unsigned char BWC_DIGIT_HI = 0x2A;   // '9'
static const unsigned char BWC_SPACE    = 0x20;

// Not a countdown. -1 rather than 0 because 0 is a number the player must hear:
// it is the last thing a countdown says.
static const int BWC_NONE = -1;

// A countdown box holds a small number. Three digits is already far past
// anything FF8 counts down from, and the cap is what stops a stray run of
// glyphs from being read as a number.
static const int BWC_MAX_DIGITS = 3;

// Shape 1: the decoded string is nothing but digits.
static int BwcFromDecoded(const char* s)
{
    if (s == nullptr) return BWC_NONE;
    int value = 0;
    int digits = 0;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return BWC_NONE;
        if (++digits > BWC_MAX_DIGITS) return BWC_NONE;
        value = value * 10 + (*p - '0');
    }
    return digits ? value : BWC_NONE;
}

// Shape 2: the raw FF8 bytes are digit glyphs, with spaces allowed around them
// because FF8 pads its window strings (the same padding that hid the v0.106.0
// off-by-one). Anything else in the string and this is not a bare number.
static int BwcFromRawBytes(const unsigned char* raw, size_t len)
{
    if (raw == nullptr) return BWC_NONE;
    int value = 0;
    int digits = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char b = raw[i];
        if (b == 0x00) break;
        if (b == BWC_SPACE) continue;
        if (b < BWC_DIGIT_LO || b > BWC_DIGIT_HI) return BWC_NONE;
        if (++digits > BWC_MAX_DIGITS) return BWC_NONE;
        value = value * 10 + (int)(b - BWC_DIGIT_LO);
    }
    return digits ? value : BWC_NONE;
}

// The decoded string first, because when the decoder succeeded it has already
// resolved everything the engine would have. The raw bytes are the fallback for
// the page it could not.
static int BwcCountdownValue(const char* decoded,
                             const unsigned char* raw, size_t rawLen)
{
    const int fromText = BwcFromDecoded(decoded);
    if (fromText != BWC_NONE) return fromText;
    return BwcFromRawBytes(raw, rawLen);
}

// Aaron's words, verbatim: "have the mod say 'Alert, Countdown #' where # is
// the digit shown on screen. This also needs to be assertive and interrupt any
// other text so it can't be missed."
static void BwcAnnounceText(char* buf, size_t n, int value)
{
    if (buf == nullptr || n == 0) return;
    if (value < 0) value = 0;
    snprintf(buf, n, "Alert, Countdown %d", value);
}

// A battle box can be written more than once with the same contents, and the
// window poll fires on a hash change that a redraw can produce without the text
// changing. Same number twice inside this window is the same countdown tick.
static const unsigned BWC_REPEAT_MS = 1200u;

static bool BwcShouldAnnounce(int value, int lastValue, unsigned msSinceLast)
{
    if (value == BWC_NONE) return false;
    if (value != lastValue) return true;
    return msSinceLast >= BWC_REPEAT_MS;
}
