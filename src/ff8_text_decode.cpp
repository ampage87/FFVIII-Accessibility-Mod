// ff8_text_decode.cpp - FF8 custom character encoding to UTF-8 decoder
//
// Character table rebuilt from the canonical Ifrit textformat.ifr:
//   https://sourceforge.net/p/ifrit/code-0/HEAD/tree/
//         trunk%20ifrit-code-0/Resources/textformat.ifr
//
// v04.00: Initial implementation for field dialog TTS.
// v04.01: Rebuilt table from Ifrit. Fixed name IDs (start at 0x30).
// v04.02: DecodeChoices via parenthesis matching (fragile).
// v04.03: Fix 0x43 backtick -> apostrophe for contractions.
//         DecodeChoices rewritten to use line-index splitting (0x02 newlines)
//         with firstQ/lastQ from win_obj for robust choice extraction.

#include "ff8_text_decode.h"
#include <cstdio>
#include <cstring>

namespace FF8TextDecode {

// ============================================================================
// Character name substitution table
// Name IDs in FF8 dialog start at 0x30 (not 0x00).
// ============================================================================

static const int NAME_ID_BASE = 0x30;

static const char* s_charNames[] = {
    "Squall",   // 0x30
    "Zell",     // 0x31
    "Irvine",   // 0x32
    "Quistis",  // 0x33
    "Rinoa",    // 0x34
    "Selphie",  // 0x35
    "Seifer",   // 0x36
    "Edea",     // 0x37
    "Laguna",   // 0x38
    "Kiros",    // 0x39
    "Ward",     // 0x3A
    "Angelo",   // 0x3B
    "Griever",  // 0x3C
    "Boko",     // 0x3D
};
static const int s_charNameCount = sizeof(s_charNames) / sizeof(s_charNames[0]);

// ============================================================================
// FF8 byte -> ASCII/UTF-8 mapping table
// ============================================================================

static char s_charTable[256] = {0};
static bool s_tableInitialized = false;

static void InitTable()
{
    if (s_tableInitialized) return;
    memset(s_charTable, 0, sizeof(s_charTable));

    // Space
    s_charTable[0x20] = ' ';

    // Digits: 0x21-0x2A -> '0'-'9'
    for (int i = 0; i <= 9; i++) {
        s_charTable[0x21 + i] = '0' + i;
    }

    // Punctuation / symbols (from Ifrit textformat.ifr)
    s_charTable[0x2B] = '%';
    s_charTable[0x2C] = '/';
    s_charTable[0x2D] = ':';
    s_charTable[0x2E] = '!';
    s_charTable[0x2F] = '?';
    // 0x30 = ellipsis (handled specially as "...")
    s_charTable[0x31] = '+';
    s_charTable[0x32] = '-';
    s_charTable[0x33] = '=';
    s_charTable[0x34] = '*';
    s_charTable[0x35] = '&';
    s_charTable[0x36] = '"';   // Japanese opening quote -> standard quote
    s_charTable[0x37] = '"';   // Japanese closing quote -> standard quote
    s_charTable[0x38] = '(';
    s_charTable[0x39] = ')';
    s_charTable[0x3A] = '\'';  // Ifrit: "' in middle" = apostrophe in contractions
    s_charTable[0x3B] = '.';   // period
    s_charTable[0x3C] = ',';   // comma
    s_charTable[0x3D] = '~';   // tilde
    s_charTable[0x3E] = '"';   // opening double quote
    s_charTable[0x3F] = '"';   // closing double quote
    s_charTable[0x40] = '\'';  // standalone single quote / apostrophe
    s_charTable[0x41] = '#';
    s_charTable[0x42] = '$';
    s_charTable[0x43] = '\'';  // Ifrit says backtick, but FF8 uses it for apostrophes
                                // in contractions (don't, won't, can't, let's, I'll).
                                // Map to apostrophe for correct TTS output.
    s_charTable[0x44] = '_';   // underscore

    // Uppercase: 0x45-0x5E -> 'A'-'Z'
    for (int i = 0; i < 26; i++) {
        s_charTable[0x45 + i] = 'A' + i;
    }

    // Lowercase: 0x5F-0x78 -> 'a'-'z'
    for (int i = 0; i < 26; i++) {
        s_charTable[0x5F + i] = 'a' + i;
    }

    s_tableInitialized = true;
}

// ============================================================================
// Internal: decode a single byte, appending to result.
// Returns how many additional bytes were consumed (0 for single-byte chars,
// 1+ for multi-byte sequences). Returns -1 to signal end of string.
// ============================================================================

static int DecodeByte(const uint8_t* data, size_t pos, size_t maxBytes,
                      std::string& result)
{
    uint8_t b = data[pos];

    // End of string
    if (b == 0x00) return -1;

    // Newline (0x01 or 0x02)
    if (b == 0x01 || b == 0x02) {
        result += ' ';
        return 0;
    }

    // Character name substitution: 0x03 + id byte
    if (b == 0x03) {
        if (pos + 1 < maxBytes) {
            uint8_t nameId = data[pos + 1];
            int index = (int)nameId - NAME_ID_BASE;
            if (index >= 0 && index < s_charNameCount) {
                result += s_charNames[index];
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "[Name%02X]", nameId);
                result += buf;
            }
        }
        return 1;
    }

    // Page break / end marker
    if (b == 0x04) {
        result += ". ";
        return 0;
    }

    // Icon codes: 0x05 + icon_id
    if (b == 0x05) return 1;

    // Color codes: 0x06 + color_id
    if (b == 0x06) return 1;

    // Control codes 0x07-0x09
    if (b >= 0x07 && b <= 0x09) return 0;

    // Special value: 0x0A + value_id
    if (b == 0x0A) return 1;

    // Choice cursor location marker: 0x0B + param
    if (b == 0x0B) return 1;

    // Spell name: 0x0C + spell_id
    if (b == 0x0C) return 1;

    // 0x0E = special marker
    if (b == 0x0E) return 0;

    // Other low control codes (0x0D-0x1F)
    if (b >= 0x0D && b <= 0x1F) return 0;

    // Ellipsis (0x30)
    if (b == 0x30) {
        result += "...";
        return 0;
    }

    // Two-char compression sequences (0xE8-0xFF from Ifrit)
    switch (b) {
        case 0xE8: result += "in"; return 0;
        case 0xE9: result += "e "; return 0;
        case 0xEA: result += "ne"; return 0;
        case 0xEB: result += "to"; return 0;
        case 0xEC: result += "re"; return 0;
        case 0xED: result += "HP"; return 0;
        case 0xEE: result += "l "; return 0;
        case 0xEF: result += "ll"; return 0;
        case 0xF0: result += "GF"; return 0;
        case 0xF1: result += "nt"; return 0;
        case 0xF2: result += "il"; return 0;
        case 0xF3: result += "o "; return 0;
        case 0xF4: result += "ef"; return 0;
        case 0xF5: result += "on"; return 0;
        case 0xF6: result += " w"; return 0;
        case 0xF7: result += " r"; return 0;
        case 0xF8: result += "wi"; return 0;
        case 0xF9: result += "fi"; return 0;
        case 0xFB: result += "s "; return 0;
        case 0xFC: result += "ar"; return 0;
        case 0xFE: result += " S"; return 0;
        case 0xFF: result += "ag"; return 0;
        default: break;
    }

    // Standard character table lookup
    char c = s_charTable[b];
    if (c != 0) {
        result += c;
    } else {
        // Unknown byte: silently skip for TTS output.
        // Bytes 0x80-0xE7 are typically control codes, extended/accented
        // characters, or JP-specific data that don't affect spoken text.
    }

    return 0;
}

// ============================================================================
// Public: Decode full string (flattened, newlines become spaces)
// ============================================================================

std::string Decode(const uint8_t* data, size_t maxBytes)
{
    if (!data) return "(null)";
    InitTable();

    std::string result;
    result.reserve(256);

    for (size_t i = 0; i < maxBytes; i++) {
        if (data[i] == 0x00) break;
        int extra = DecodeByte(data, i, maxBytes, result);
        if (extra < 0) break;
        i += extra;
    }

    return result;
}

// ============================================================================
// Public: Decode into separate lines (split on 0x02 newline bytes)
//
// FF8 dialog text uses 0x02 as a line separator. Each line may be:
//   - Speaker name (e.g. "Dr. Kadowaki")
//   - Dialog text (e.g. "How are you feeling?")
//   - A choice option (e.g. "(Ok, I guess)")
//
// The win_obj's firstQ/lastQ fields tell us which lines are choices
// (0-indexed line numbers). Everything else is the prompt.
// ============================================================================

std::vector<std::string> DecodeLines(const uint8_t* data, size_t maxBytes)
{
    std::vector<std::string> lines;
    if (!data) return lines;
    InitTable();

    std::string current;
    current.reserve(128);

    for (size_t i = 0; i < maxBytes; i++) {
        uint8_t b = data[i];
        if (b == 0x00) break;

        // Line break: 0x02 starts a new line
        if (b == 0x02) {
            lines.push_back(current);
            current.clear();
            continue;
        }

        // 0x01 also appears as newline in some contexts — treat same
        if (b == 0x01) {
            lines.push_back(current);
            current.clear();
            continue;
        }

        // Decode normally (appends to current)
        int extra = DecodeByte(data, i, maxBytes, current);
        if (extra < 0) break;
        i += extra;
    }

    // Don't forget the last line (text after final newline)
    if (!current.empty()) {
        lines.push_back(current);
    }

    // Trim each line
    for (auto& line : lines) {
        size_t start = line.find_first_not_of(" ");
        size_t end = line.find_last_not_of(" ");
        if (start == std::string::npos) {
            line.clear();
        } else {
            line = line.substr(start, end - start + 1);
        }
    }

    return lines;
}

// ============================================================================
// Public: Decode with choice splitting for ASK/AASK opcodes
//
// Uses firstQ/lastQ line indices from the caller to determine which
// lines are choices. Lines before firstQ are the prompt; lines from
// firstQ to lastQ (inclusive) are choice options.
//
// If firstQ/lastQ are both 0, falls back to returning everything as prompt.
// ============================================================================

ChoiceDialog DecodeChoices(const uint8_t* data, size_t maxBytes,
                           uint8_t firstQ, uint8_t lastQ)
{
    ChoiceDialog dialog;
    if (!data) return dialog;

    std::vector<std::string> lines = DecodeLines(data, maxBytes);
    if (lines.empty()) return dialog;

    // Build prompt from all lines BEFORE firstQ
    for (int i = 0; i < (int)firstQ && i < (int)lines.size(); i++) {
        if (!lines[i].empty()) {
            if (!dialog.prompt.empty()) dialog.prompt += " ";
            dialog.prompt += lines[i];
        }
    }

    // Extract choice lines from firstQ to lastQ (inclusive)
    for (int i = (int)firstQ; i <= (int)lastQ && i < (int)lines.size(); i++) {
        if (!lines[i].empty()) {
            // Strip surrounding parentheses if present (common in FF8 choices)
            std::string choice = lines[i];
            if (choice.size() >= 2 && choice.front() == '(' && choice.back() == ')') {
                choice = choice.substr(1, choice.size() - 2);
                // Trim again after removing parens
                size_t s = choice.find_first_not_of(" ");
                size_t e = choice.find_last_not_of(" ");
                if (s != std::string::npos)
                    choice = choice.substr(s, e - s + 1);
            }
            dialog.choices.push_back(choice);
        }
    }

    // If prompt is empty but we have lines, use all non-choice lines
    if (dialog.prompt.empty()) {
        for (int i = 0; i < (int)lines.size(); i++) {
            if (i >= (int)firstQ && i <= (int)lastQ) continue;
            if (!lines[i].empty()) {
                if (!dialog.prompt.empty()) dialog.prompt += " ";
                dialog.prompt += lines[i];
            }
        }
    }

    return dialog;
}

// ============================================================================
// Utility functions
// ============================================================================

std::string DecodeWithHex(const uint8_t* data, size_t maxBytes, std::string& hexDump)
{
    hexDump = HexDump(data, maxBytes);
    return Decode(data, maxBytes);
}

std::string HexDump(const uint8_t* data, size_t count)
{
    if (!data) return "(null)";

    std::string result;
    result.reserve(count * 3 + 1);

    for (size_t i = 0; i < count; i++) {
        if (data[i] == 0x00) break;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        result += buf;
    }

    if (!result.empty() && result.back() == ' ')
        result.pop_back();

    return result;
}

// ============================================================================
// Menu font (sysfnt) glyph-to-character table (v07.11)
// Source: myst6re/deling — src/qt/fonts/sysfnt.txt
// 14 rows x 16 columns = 224 entries. Index = row*16 + col.
// ============================================================================

static const char* s_menuGlyphTable[224] = {
    // Row 0 (0x00-0x0F): space, digits, punctuation
    " ","0","1","2","3","4","5","6","7","8","9","%","/",":","!","?",
    // Row 1 (0x10-0x1F): symbols
    "...","+","-","=","*","&","","","(",")"," ",".",",","~","","",
    // Row 2 (0x20-0x2F): punctuation + uppercase A-K
    "'","#","$","'","_","A","B","C","D","E","F","G","H","I","J","K",
    // Row 3 (0x30-0x3F): uppercase L-Z + lowercase a
    "L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","a",
    // Row 4 (0x40-0x4F): lowercase b-q
    "b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q",
    // Row 5 (0x50-0x5F): lowercase r-z + accented uppercase
    "r","s","t","u","v","w","x","y","z","A","A","A","A","C","E","E",
    // Row 6 (0x60-0x6F): accented uppercase continued (simplified to ASCII)
    "E","E","I","I","I","I","N","O","O","O","O","U","U","U","U","OE",
    // Row 7 (0x70-0x7F): accented lowercase (simplified to ASCII)
    "ss","a","a","a","a","c","e","e","e","e","i","i","i","i","n","o",
    // Row 8 (0x80-0x8F): accented lowercase continued + symbols
    "o","o","o","u","u","u","u","oe","","[","]","","","","","",
    // Row 9 (0x90-0x9F): symbols
    "","","","","",";","","","x","","",""," degrees","","","-",
    // Row 10 (0xA0-0xAF): symbols
    "","","+/-","","","","","","","TM","<",">","","","","",
    // Row 11 (0xB0-0xBF): mostly empty
    "","","","","","","","","","","","","","","","",
    // Row 12 (0xC0-0xCF): empty + compression sequences
    "","","","","","","","","in","e ","ne","to","re","HP","l ","ll",
    // Row 13 (0xD0-0xDF): compression sequences
    "GF","nt","il","o ","ef","on"," w"," r","wi","fi","","s ","ar",""," S","ag"
};

std::string DecodeMenuText(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return "";

    std::string result;
    result.reserve(len);
    bool lastWasSpace = false;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        // Glyph indices 0x00-0xDF map into the 224-entry table
        const char* ch = nullptr;
        if (b < 224) {
            ch = s_menuGlyphTable[b];
        }

        if (ch && ch[0] != '\0') {
            // Collapse repeated spaces
            if (ch[0] == ' ' && ch[1] == '\0') {
                if (!lastWasSpace) {
                    result += ' ';
                    lastWasSpace = true;
                }
            } else {
                result += ch;
                // Check if this multi-char entry ends with a space
                size_t slen = strlen(ch);
                lastWasSpace = (ch[slen - 1] == ' ');
            }
        }
        // else: unknown glyph — skip silently
    }

    // Trim trailing space
    while (!result.empty() && result.back() == ' ')
        result.pop_back();

    return result;
}

}  // namespace FF8TextDecode
