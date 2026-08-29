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
#if defined(_WIN32)
#include <windows.h>   // v0.35.0: SEH around the engine's word-table pointer
#endif

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

// ============================================================================
// FF8 raw-byte -> text table, DERIVED from the glyph table above (v0.35.0)
// ============================================================================
//
// The two tables were never independent. The engine's glyph drawer
// (`0x004BDE30`) turns a raw stream byte into a font cell with
//
//     glyph = byte - 0x20            (for every byte >= 0x20)
//
// so the raw encoding IS the sysfnt grid, shifted by 0x20. That was verified
// against the two hand-built tables this file used to carry: of the ~90 bytes
// both of them defined, they agreed on every one except five slots where the
// Deling grid is blank (0x36/0x37/0x3E/0x3F quotes, 0x3A separator -- see the
// v0.117.0 note at InitTable, which corrects the original "apostrophe" guess) and two
// where it is blank for a compression pair (0xFA "EC", 0xFD "FE"). Those seven
// are the override list below; everything else now comes from ONE table.
//
// The win is the 57 bytes the old hand-built table had no entry for at all --
// 0x79..0xA7 (accented letters), 0xA9/0xAA (brackets), 0xB5 (semicolon),
// 0xB8 ('x'), 0xBC (degrees), 0xBF, 0xC2, 0xC9..0xCB. Every one of them was
// SILENTLY DROPPED, which is half of what made shop item descriptions come out
// as fragments (v0.34.8/.9). They are ordinary printable characters and the
// grid has always known what they are.
// ============================================================================

// ============================================================================
// BUTTON ICONS -> THE KEY THE PLAYER IS ACTUALLY HOLDING (v0.67.3)
// ============================================================================
// See the 0x05 branch below for why this exists and where the addresses come
// from. Everything here is a read: one byte out of the live keymap, and a
// lookup in a table of our own.

static const uintptr_t BTN_KEYMAP_BASE   = 0x01CD0208;  // + device*0x20 + button
static const uintptr_t BTN_DEVICE_FLAG   = 0x00B8600E;  // non-zero: a pad was last used
static const uintptr_t BTN_REMAP_FLAGS   = 0x01CFE73C;  // bit 0x20: custom config in use
static const uintptr_t BTN_REMAP_TABLE   = 0x01CFE740;  // 12 bytes, value = action + 1

// DirectInput scancode -> something worth saying out loud. The engine's own
// table is four characters wide and calls half of these "NONE".
struct DikName { uint8_t code; const char* name; };
static const DikName DIK_NAMES[] = {
    {0x01,"Escape"},{0x02,"1"},{0x03,"2"},{0x04,"3"},{0x05,"4"},{0x06,"5"},
    {0x07,"6"},{0x08,"7"},{0x09,"8"},{0x0A,"9"},{0x0B,"0"},{0x0C,"minus"},
    {0x0D,"equals"},{0x0E,"Backspace"},{0x0F,"Tab"},
    {0x10,"Q"},{0x11,"W"},{0x12,"E"},{0x13,"R"},{0x14,"T"},{0x15,"Y"},
    {0x16,"U"},{0x17,"I"},{0x18,"O"},{0x19,"P"},
    {0x1A,"left bracket"},{0x1B,"right bracket"},{0x1C,"Enter"},{0x1D,"left Control"},
    {0x1E,"A"},{0x1F,"S"},{0x20,"D"},{0x21,"F"},{0x22,"G"},{0x23,"H"},
    {0x24,"J"},{0x25,"K"},{0x26,"L"},{0x27,"semicolon"},{0x28,"apostrophe"},
    {0x29,"backtick"},{0x2A,"left Shift"},{0x2B,"backslash"},
    {0x2C,"Z"},{0x2D,"X"},{0x2E,"C"},{0x2F,"V"},{0x30,"B"},{0x31,"N"},{0x32,"M"},
    {0x33,"comma"},{0x34,"full stop"},{0x35,"slash"},{0x36,"right Shift"},
    {0x37,"numpad star"},{0x38,"left Alt"},{0x39,"Space"},{0x3A,"Caps Lock"},
    {0x3B,"F1"},{0x3C,"F2"},{0x3D,"F3"},{0x3E,"F4"},{0x3F,"F5"},{0x40,"F6"},
    {0x41,"F7"},{0x42,"F8"},{0x43,"F9"},{0x44,"F10"},
    {0x47,"numpad 7"},{0x48,"numpad 8"},{0x49,"numpad 9"},{0x4A,"numpad minus"},
    {0x4B,"numpad 4"},{0x4C,"numpad 5"},{0x4D,"numpad 6"},{0x4E,"numpad plus"},
    {0x4F,"numpad 1"},{0x50,"numpad 2"},{0x51,"numpad 3"},{0x52,"numpad 0"},
    {0x53,"numpad full stop"},{0x57,"F11"},{0x58,"F12"},
    {0x9C,"numpad Enter"},{0x9D,"right Control"},{0xB5,"numpad slash"},
    {0xB8,"right Alt"},{0xC7,"Home"},{0xC8,"Up arrow"},{0xC9,"Page Up"},
    {0xCB,"Left arrow"},{0xCD,"Right arrow"},{0xCF,"End"},{0xD0,"Down arrow"},
    {0xD1,"Page Down"},{0xD2,"Insert"},{0xD3,"Delete"},
    {0,nullptr}
};

// ONE READ PATH, so a probe can supply the memory.
//
// Every byte this resolution needs lives at a fixed engine address, which makes
// it exactly the sort of code that only ever gets checked by playing the game --
// and this one decides what a blind player is told to press. The indirection
// costs a null test and buys a test that can put a keymap in front of it and
// assert the sentence that comes out.
typedef uint8_t (*ButtonPeekFn)(uintptr_t);
static ButtonPeekFn s_btnPeek = nullptr;

static uint8_t BtnPeek(uintptr_t a)
{
    if (s_btnPeek) return s_btnPeek(a);
#if defined(_WIN32)
    __try { return *(volatile const uint8_t*)a; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
#else
    (void)a;
    return 0;      // off-Windows and unhooked: name nothing rather than crash
#endif
}

void SetButtonPeekHook(ButtonPeekFn fn) { s_btnPeek = (ButtonPeekFn)fn; }

// param -> pad button 0..15, or -1 when the parameter is not a button at all
// (the 0x40+ range is ordinary picture icons; no field text uses it).
static int ButtonForIconParam(uint8_t param)
{
    if (param >= 0x30 && param <= 0x3F) return param - 0x30;   // the button itself
    if (param < 0x20 || param > 0x2F)   return -1;
    const int action = param - 0x20;
    const uint8_t flags = BtnPeek(BTN_REMAP_FLAGS);
    if (!(flags & 0x20) || action >= 0x0C) return action;      // remap off: identity
    for (int i = 0; i < 12; i++)
        if (BtnPeek(BTN_REMAP_TABLE + (uintptr_t)i) == (uint8_t)(action + 1))
            return i;
    return -1;                                                // action is unassigned
}

// The whole resolution. False when there is nothing honest to say.
// v0.120.0 (#centra): the second half of DecodeButtonKey, split out so a caller
// that already HAS a button index can reach it. Auto-drive does: a Centra
// ladder's BTNTEST mask names buttons 6 and 7 directly, with no 0x05 icon param
// anywhere in sight, and telling a keyboard player to "press Cross" would be
// useless. Same keymap, same fallbacks, same engine behaviour.
bool ButtonKeyName(int b, char* out, size_t n)
{
    if (!out || n < 8) return false;
    out[0] = '\0';
    if (b < 0 || b > 15) return false;

    const int dev = BtnPeek(BTN_DEVICE_FLAG) ? 1 : 0;
    uint8_t code = BtnPeek(BTN_KEYMAP_BASE + (uintptr_t)dev * 0x20 + (uintptr_t)b);
    if (!code)      // a pad button with no binding falls back to the keyboard,
        code = BtnPeek(BTN_KEYMAP_BASE + (uintptr_t)b);        // as the engine does
    if (!code) return false;

    for (const DikName* d = DIK_NAMES; d->name; d++) {
        if (d->code != code) continue;
        snprintf(out, n, "%s", d->name);
        return true;
    }
    // The joystick range, which the engine numbers B1..B28.
    if (code >= 0xE0 && code <= 0xFB) { snprintf(out, n, "button %d", code - 0xE0 + 1); return true; }
    if (code == 0xFC) { snprintf(out, n, "stick up");    return true; }
    if (code == 0xFD) { snprintf(out, n, "stick down");  return true; }
    if (code == 0xFE) { snprintf(out, n, "stick left");  return true; }
    if (code == 0xFF) { snprintf(out, n, "stick right"); return true; }
    return false;   // a scancode we have no word for: say nothing, count it lost
}

// The 0x05 icon path: an icon param names an ACTION, which the remap table
// turns into a button, which ButtonKeyName above turns into a key.
static bool DecodeButtonKey(uint8_t param, char* out, size_t n)
{
    return ButtonKeyName(ButtonForIconParam(param), out, n);
}

static const char* s_charTable[256] = {0};
static bool s_tableInitialized = false;

static void InitTable()
{
    if (s_tableInitialized) return;

    for (int b = 0x20; b <= 0xFF; b++) {
        const int glyph = b - 0x20;                 // 0x00 .. 0xDF
        const char* g = s_menuGlyphTable[glyph];
        s_charTable[b] = (g && g[0]) ? g : nullptr;
    }

    // The seven slots where the Deling grid is blank but the raw encoding is
    // not (Ifrit textformat.ifr, and the FF8 text this mod already reads).
    s_charTable[0x36] = "\"";   // Japanese opening quote -> standard quote
    s_charTable[0x37] = "\"";   // Japanese closing quote
    // v0.117.0 (#centra): 0x3A IS NOT AN APOSTROPHE, AND NEVER WAS.
    //
    // Aaron, after the Centra Ruins: "the code [is] not reading out when I put
    // both eyes in the top statue. When you do that it displays the code
    // visually but isn't reading it." Half of why is here. crroof1 message 9 is
    //
    //   47 6d 62 63 2d  04 20  3A  04 21  3A  04 22  3A  04 23  3A  04 24
    //   C  o  d  e  :   {n0}   ?   {n1}   ?   {n2}   ?   {n3}   ?   {n4}
    //
    // -- five numeric inserts with 0x3A between them, which under the old
    // mapping spoke as "Code:9'8'9'3'9".
    //
    // This slot is one of the seven where the Deling grid is blank, so v04.01
    // guessed, and the guess came with a justification -- "inside contractions"
    // -- that the disc disproves. Across all 900 fields' .msd there are 83
    // occurrences of 0x3A and NOT ONE is a contraction: 34 separate words in
    // debug labels ("Ship?outside", "Norg?Angry", "Shaken?loop", "SEAL?tear?
    // Centera"), 8 separate the Centra code digits, 7 separate debug label from
    // id ("Agit4?0162"). Contractions use **0x43**, which v04.03 already fixed
    // and which "don't" alone uses 822 times.
    //
    // Nor is it a colon: "Garden hit:Shaken?loop" carries a real colon (0x2D)
    // and an 0x3A in the same string, doing different jobs. Every one of the 83
    // reads correctly as a SPACE, and a space is the reading that cannot mislead
    // a screen reader -- "Code: 9 8 9 3 9" rather than five digits glued
    // together by punctuation.
    s_charTable[0x3A] = " ";    // word/number separator (NOT an apostrophe)
    s_charTable[0x3E] = "\"";   // opening double quote
    s_charTable[0x3F] = "\"";   // closing double quote
    s_charTable[0xFA] = "EC";   // compression pair, blank in the grid
    s_charTable[0xFD] = "FE";   // compression pair, blank in the grid

    s_tableInitialized = true;
}

// ============================================================================
// v0.35.0 (#93): THE 0x0E WORD TABLE -- namedic.bin
// ============================================================================
//
// Every menu, battle and field string in FF8 is expanded by ONE routine before
// it reaches the glyph drawer: `sub_4B8B30(src, dst, maxLen)`. The shop's own
// draw callback shows the shape --
//
//   0x004ED534  mov ecx, [0x1D76AA0]        ; the description pointer
//   0x004ED56D  call 0x4B8B30               ; expand into a 0x80-byte buffer
//   0x004ED58B  call 0x4BDE30               ; draw the EXPANDED bytes
//
// which is why no `cmp al, 0x0E` exists anywhere near the drawer: the drawer
// never sees one. The expander's tail case is the substitution:
//
//   0x004B8CB8  cmp ecx, 0xE / jl ...       ; codes >= 0x0E land here
//   0x004B8CBD  mov dl, [ebp] / inc ebp     ; read + consume the param byte
//   0x004B8CC0  add ecx, -0xE               ; group = code - 0x0E
//   0x004B8CC3  sub dl, 0x20                ; idx   = param - 0x20
//   0x004B8CC7+ eax = group*7, shl 5        ; entry = group*224 + idx
//   0x004B8CD6  mov ecx, [0x1D2B80C]        ; the table pointer
//   0x004B8CE4  test ecx, ecx / je          ; null table -> emit NOTHING
//   0x004B8CF1  movzx edx, word [ecx]       ; entry count (first word)
//   0x004B8CF4  cmp edx, eax / jle          ; out of range -> emit NOTHING
//   0x004B8D01  movzx esi, word [ecx+eax*2+2]  ; offset, relative to the table
//   0x004B8D06  add esi, ecx                ; -> an FF8-encoded string
//   0x004B8D0A  call 0x49A740 / 0x49A790    ; splice it into dst
//
// THE TABLE IS A FILE. `[0x01D2B80C]` is set at 0x004A1980 from `[0x00B6D060]`,
// the load buffer that 0x0047D46C fills from a filename assembled out of
// `[0x00B81024]` = **"namedic.bin"** -- `main.fs` entry 13, 408 bytes,
// LZS-compressed. Its layout is exactly what the code reads: `u16 count`, then
// `count` x `u16 offset`, then packed NUL-terminated FF8 strings. The shipped
// English file holds 32 words:
//
//   0..18  place names (Galbadia, Esthar, Balamb, Dollet, Timber, Trabia,
//          Centra, Fishermans Horizon, East Academy, Desert Prison, Trabia
//          Garden, Lunar Base, Shumi Village, Deling City, Balamb Garden,
//          East Academy Station, Dollet Station, Desert Prison Station,
//          Lunar Gate)
//   19..31 Restores, status, learns, ability, Magic, Refine, Junctions,
//          Raises, command, Magazine, Ultimecia Castle, Garden, Deling
//
// So the ten bytes from the v0.34.9 BAT --
//
//   F0 20 0E 35 20 0E 37 20 0E 36
//
// are "GF" + " " + entry 0x15 + " " + entry 0x17 + " " + entry 0x16, i.e.
// **"GF learns Magic ability"**, the Magic Scroll's description. Decoding the
// shipped kernel.bin with this table turns all 88 of its 0x0E strings into
// whole English sentences ("Restores 1000 HP to GF", "Makes GF forget an
// ability", "Cures abnormal status and Magic effects").
//
// WE READ THE ENGINE'S OWN POINTER rather than shipping a copy of the file.
// The lookup then cannot drift from what is on screen, tracks any localisation
// or text mod, and reproduces the engine's two silent-skip cases byte for byte
// (null table / entry >= count) so a failed lookup emits exactly what the game
// emits: nothing. `SetWordTableBase()` exists so the host probe can point the
// same code at a real namedic image without a running game.
// ============================================================================

static const uintptr_t FF8_WORD_TABLE_PTR = 0x01D2B80C;
static const uint8_t*  s_wordTableBase    = nullptr;   // probe override

void SetWordTableBase(const void* base)
{
    s_wordTableBase = (const uint8_t*)base;
}

// Read the table base the engine is currently using. SEH-guarded, and holds no
// object with a destructor (MSVC C2712 -- see tests/lint_seh.py).
static const uint8_t* WordTableBase()
{
    if (s_wordTableBase) return s_wordTableBase;
#if defined(_WIN32)
    const uint8_t* p = nullptr;
    __try {
        p = *(const uint8_t* volatile*)FF8_WORD_TABLE_PTR;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p = nullptr;
    }
    return p;
#else
    return nullptr;   // host probes: only the override is available
#endif
}

size_t ResolveWord(uint8_t code, uint8_t param, uint8_t* out, size_t outSize)
{
    size_t written = 0;
    if (!out || outSize == 0) return 0;
    out[0] = 0x00;
    if (code < 0x0E || code > 0x0F) return 0;

    const uint8_t* table = WordTableBase();
    if (!table) return 0;

#if defined(_WIN32)
    __try {
#endif
        const uint32_t entry = (uint32_t)(code - 0x0E) * 224u
                             + (uint32_t)(uint8_t)(param - 0x20);
        const uint16_t count = *(const volatile uint16_t*)table;
        if (entry < (uint32_t)count) {
            const uint16_t off = *(const volatile uint16_t*)(table + 2 + entry * 2);
            const uint8_t* srcp = table + off;
            for (; written + 1 < outSize; written++) {
                const uint8_t sb = srcp[written];
                if (sb == 0x00) break;
                out[written] = sb;
            }
        }
#if defined(_WIN32)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
    }
#endif
    if (written < outSize) out[written] = 0x00;
    return written;
}

// ============================================================================
// v0.38.0 (#97): THE NAMEABLE NAMES -- ANGELO, GRIEVER, BOKO, SQUALL, RINOA
// ============================================================================
//
// Aaron: *"when Rinoa's limit triggers it is saying 'Name 40' instead of
// Angelo... I think it also said Name 40 in the magazine viewer with Pet Pals."*
// `[Name40]` is this file's own placeholder for a `0x03` name code whose id
// falls outside the fourteen-entry party table.
//
// The engine's dispatch for `0x03 xx` is a ladder, and it is explicit about
// which names come out of the SAVEMAP:
//
//   004B8D94  cmp ecx, 0x30 / jl  ->  004B8D99  cmp ecx, 0x3f / jg
//   004B8D9E  add ecx, -0x10 / and ecx, 0x1F
//   004B8DA5  call 0x0049A840  ->  0x0047EB50(idx):
//                idx 0  -> 0x01CFDC70     Squall   (renameable)
//                idx 4  -> 0x01CFDC7C     Rinoa    (renameable)
//                else   -> kernel table 0x01CF75EC + idx*36
//   004B8DAC  cmp ecx, 0x40 / je  ->  eax = 0x01CFDC88     <- ANGELO
//   004B8DB1  cmp ecx, 0x50 / je  ->  eax = 0x01CFE754     <- GRIEVER
//   004B8DB6  cmp ecx, 0x60 / je  ->  eax = 0x01CFDC94     <- BOKO
//   004B8DBB  otherwise           ->  eax = 0x01D76624     (scratch)
//
// All five addresses read back exactly those names from Aaron's own save file,
// which is what turns a plausible ladder into a settled one:
//
//   0x01CFDC70 "Squall"   0x01CFDC7C "Rinoa"   0x01CFDC88 "Angelo"
//   0x01CFE754 "Griever"  0x01CFDC94 "Boko"
//
// So `[Name40]` was never a party member at a missing index -- **0x40 is not an
// index into that table at all.** It is a separate branch of the ladder, and so
// are 0x50 and 0x60, which would have read as `[Name50]` and `[Name60]` the
// moment a Griever or Boko line came up.
//
// **These five are read live**, not hardcoded, because they are precisely the
// names a player can change; the other ten come from a kernel table and stay on
// the built-in list. `SetNameTableBase()` lets the probe point the same code at
// a fixture, the way `SetWordTableBase()` already does for namedic.
// ============================================================================

// savemap-relative so the probe can supply one block instead of five addresses.
static const uintptr_t FF8_SAVEMAP_BASE = 0x01CFDC5C;
static const int NAME_OFF_SQUALL  = 0x14;   // 0x01CFDC70
static const int NAME_OFF_RINOA   = 0x20;   // 0x01CFDC7C
static const int NAME_OFF_ANGELO  = 0x2C;   // 0x01CFDC88
static const int NAME_OFF_BOKO    = 0x38;   // 0x01CFDC94
static const int NAME_OFF_GRIEVER = 0xAF8;  // 0x01CFE754

static const uint8_t* s_nameTableBase = nullptr;   // probe override

void SetNameTableBase(const void* base)
{
    s_nameTableBase = (const uint8_t*)base;
}

// Byte offset from the savemap base for a 0x03 name id, or -1 when the id is
// not one of the five the player can change.
static int NameOffsetFor(uint8_t nameId)
{
    switch (nameId) {
        case 0x30: return NAME_OFF_SQUALL;
        case 0x34: return NAME_OFF_RINOA;
        case 0x40: return NAME_OFF_ANGELO;
        case 0x50: return NAME_OFF_GRIEVER;
        case 0x60: return NAME_OFF_BOKO;
        default:   return -1;
    }
}

// SEH-guarded, no object with a destructor (MSVC C2712 -- tests/lint_seh.py).
static size_t ResolveNameBytes(uint8_t nameId, uint8_t* out, size_t outSize)
{
    size_t written = 0;
    if (!out || outSize == 0) return 0;
    out[0] = 0x00;

    const int off = NameOffsetFor(nameId);
    if (off < 0) return 0;

    const uint8_t* base = s_nameTableBase;
#if defined(_WIN32)
    if (!base) base = (const uint8_t*)FF8_SAVEMAP_BASE;
#else
    if (!base) return 0;      // host probes: only the override is available
#endif

#if defined(_WIN32)
    __try {
#endif
        const uint8_t* p = base + off;
        for (; written + 1 < outSize; written++) {
            const uint8_t b = p[written];
            if (b == 0x00) break;
            out[written] = b;
        }
#if defined(_WIN32)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
    }
#endif
    if (written < outSize) out[written] = 0x00;
    return written;
}

// ============================================================================
// Internal: decode a single byte, appending to result.
// Returns how many additional bytes were consumed (0 for single-byte chars,
// 1+ for multi-byte sequences). Returns -1 to signal end of string.
//
// `dropped`, when non-null, counts bytes that were consumed but produced NO
// text -- a word the table could not resolve, an unmapped glyph, a name insert
// this decoder cannot expand. It is the only honest answer to "is what I am
// about to speak the whole sentence?", and the shop reader uses it to decide
// between a description and a "Partial description:" warning. Codes that carry
// no text by design (icons, colours, the choice-cursor marker, line breaks) are
// NOT counted -- nothing is lost when they are dropped.
// ============================================================================

static int DecodeByte(const uint8_t* data, size_t pos, size_t maxBytes,
                      std::string& result, int* dropped = nullptr)
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
            const uint8_t nameId = data[pos + 1];

            // v0.38.0: the five names the player can change come out of the
            // savemap, exactly where the engine's ladder points (see above).
            // Angelo is 0x40 -- NOT an index into the party table, which is why
            // it used to read as "[Name40]".
            uint8_t nameBytes[32];
            const size_t n = ResolveNameBytes(nameId, nameBytes, sizeof(nameBytes));
            if (n > 0) {
                for (size_t k = 0; k < n; k++) {
                    const uint8_t nb = nameBytes[k];
                    if (nb >= 0x20 && s_charTable[nb]) result += s_charTable[nb];
                }
                return 1;
            }

            const int index = (int)nameId - NAME_ID_BASE;
            if (index >= 0 && index < s_charNameCount) {
                result += s_charNames[index];
            } else {
                // Still not a name we can resolve. Say so rather than inventing
                // one -- but count it, so a caller that reads the line aloud
                // knows it is incomplete.
                char buf[16];
                snprintf(buf, sizeof(buf), "[Name%02X]", nameId);
                result += buf;
                if (dropped) (*dropped)++;
            }
        }
        return 1;
    }

    // 0x04 + param: numeric insert (sub_4B8E40). The FIELD path pre-expands
    // these before calling us (field_dialog_expand.inl, #77) and the value has
    // no meaning outside that context, so here the number is genuinely lost.
    // The historical ". " is kept -- every field dialog in the mod has been
    // heard with it and changing it is a separate, BAT-able decision -- but the
    // loss is now COUNTED rather than passed off as a complete sentence.
    if (b == 0x04) {
        result += ". ";
        if (dropped) (*dropped)++;
        return 0;
    }

    // ------------------------------------------------------------------
    // 0x05 + param: THE BUTTON ICON, WHICH IS NOT "no text by design"
    // ------------------------------------------------------------------
    // Aaron, 2026-08-23, on the Trabia dragon's two legend windows: *"the legend
    // doesn't announce the actual controls. Can we ensure as those legend items
    // appear they also announce their respective control?"* They did not,
    // because this decoder dropped the icon and left the sentence with no
    // subject: `tvglen3` string 23 is `05 27` + " to defend!", and what he heard
    // was "to defend!". A sighted player sees a green A there.
    //
    // This is not one scene. A scan of all 841 field .msd files finds 183 of
    // these across the game -- "{Square} to defend!", "Press the {Select}",
    // "{Start} to end concert", "{L1} to quit" -- every one of them a prompt
    // whose whole content is the icon.
    //
    // HOW THE GAME RESOLVES IT (sub_49F930 / sub_4A2F80 / sub_468260):
    //   param 0x20..0x2F   an ACTION index; remapped through the savemap
    //   param 0x30..0x3F   the pad BUTTON directly
    //   button b           0=L2 1=R2 2=L1 3=R1 4=Triangle 5=Circle 6=Cross
    //                      7=Square 8=Select 9=L3 10=R3 11=Start
    //                      12=Up 13=Right 14=Down 15=Left
    //   key                byte [0x01CD0208 + device*0x20 + b], a DirectInput
    //                      scancode; device 1 when a pad was last used
    //                      ([0x00B8600E]), else 0, falling back to 0
    //
    // Verified against his screenshot: `05 24` is action 4 = Triangle, keymap
    // 0x11 = W; `05 27` is action 7 = Square, keymap 0x1E = A. The box on screen
    // read "W to attack!" and "A to defend!".
    //
    // WE NAME THE SCANCODE OURSELVES rather than reading the engine's name
    // table at 0x00B86010. Two reasons, and the second is the important one:
    // chasing a char* out of a table in the exe is a pointer dereference this
    // decoder has no business making, and the engine's table is written for a
    // four-character on-screen slot -- Escape is "ES", numpad 7 is "N7", and
    // Enter, Space, Backspace and both Page keys are all literally "NONE".
    // Reading "NONE to defend" to a blind player would be worse than the
    // silence it replaces.
    if (b == 0x05) {
        if (pos + 1 >= maxBytes) return 1;      // truncated string: nothing to name
        char key[32];
        if (DecodeButtonKey(data[pos + 1], key, sizeof key) && key[0]) {
            result += key;
        } else if (dropped) {
            (*dropped)++;      // an icon we could not name is still a loss
        }
        return 1;
    }

    // Color codes: 0x06 + color_id  (no text by design)
    if (b == 0x06) return 1;

    // 0x07: the expander EXITS on it before reading anything (0x004B8B9B),
    // so it consumes no parameter. 0x08 / 0x09 fall through to 0x004B8D21,
    // which reads and consumes one -- the same path 0x05/0x06/0x0A/0x0B take,
    // and the two this decoder used to leave behind.
    if (b == 0x07) return 0;
    if (b == 0x08 || b == 0x09) return 1;

    // Special value: 0x0A + value_id
    if (b == 0x0A) { if (dropped) (*dropped)++; return 1; }

    // Choice cursor location marker: 0x0B + param  (no text by design)
    if (b == 0x0B) return 1;

    // 0x0C / 0x0D + param: location / name inserts. The engine resolves these
    // through sub_47E970 and sub_47EA30; the field path calls those directly
    // (#78). Here the param is consumed -- which is what the engine's reader
    // does for every code 0x02..0x0F -- and the name is counted as lost.
    // v0.35.0: 0x0D used to fall into the 0x0D-0x1F bucket and consume NOTHING,
    // so its param byte leaked out as a stray character.
    if (b == 0x0C || b == 0x0D) { if (dropped) (*dropped)++; return 1; }

    // 0x0E / 0x0F + param: SUBSTITUTED WORD from namedic.bin (v0.35.0, #93).
    // See the disassembly block above. The engine splices the word's own
    // FF8-encoded bytes into the stream, so we decode them the same way -- one
    // level only, which is all the shipped table needs (its entries are plain
    // glyph bytes) and which cannot recurse.
    if (b == 0x0E || b == 0x0F) {
        if (pos + 1 < maxBytes) {
            uint8_t word[128];
            size_t n = ResolveWord(b, data[pos + 1], word, sizeof(word));
            if (n > 0) {
                for (size_t k = 0; k < n; k++) {
                    uint8_t wb = word[k];
                    if (wb >= 0x20 && s_charTable[wb]) result += s_charTable[wb];
                    else if (wb == 0x01 || wb == 0x02) result += ' ';
                }
            } else if (dropped) {
                (*dropped)++;
            }
        }
        return 1;
    }

    // Name codes 0x10-0x1F: the engine resolves these through sub_49A860 with
    // (code & 0x1F) and consumes NO param byte (0x004B8D2D onward).
    if (b >= 0x10 && b <= 0x1F) { if (dropped) (*dropped)++; return 0; }

    // v0.40.0 (#101): 0xAE / 0xAF are an EMPHASIS PAIR, not text and not a
    // loss. They wrap a highlighted term -- `{AE}Save Point{AF}`,
    // `{AE}CONFIRM EQUIPMENT{AF}`, `{AE}missile launcher{AF}` -- and the Deling
    // grid has them blank because they draw no glyph. Counting them as dropped
    // made every one of those strings report two lost bytes, which is what a
    // caller like the shop reader uses to decide it is holding a fragment. They
    // are consumed silently, the way colour and icon codes already are.
    if (b == 0xAE || b == 0xAF) return 0;

    // Standard character table lookup. Since v0.35.0 the table is derived from
    // the sysfnt glyph grid (glyph = byte - 0x20), so the accented letters,
    // brackets and symbols in 0x79..0xCB resolve instead of vanishing, and the
    // 0xE8..0xFF two-character compression pairs come from the same place.
    const char* c = s_charTable[b];
    if (c) {
        result += c;
    } else if (dropped) {
        (*dropped)++;   // a byte we have no character for -- say so
    }

    return 0;
}

// ============================================================================
// Public: Decode full string (flattened, newlines become spaces)
// ============================================================================

std::string Decode(const uint8_t* data, size_t maxBytes, int* droppedOut)
{
    if (droppedOut) *droppedOut = 0;
    if (!data) return "(null)";
    InitTable();

    std::string result;
    result.reserve(256);

    for (size_t i = 0; i < maxBytes; i++) {
        if (data[i] == 0x00) break;
        int extra = DecodeByte(data, i, maxBytes, result, droppedOut);
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
