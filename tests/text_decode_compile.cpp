// text_decode_compile.cpp -- host probe for FF8TextDecode (v0.35.0, #93)
//
// WHY THIS EXISTS
//
// The decoder is the one piece of this mod that EVERY screen depends on and
// that no probe has ever exercised. Ten of the eleven menu probes stub it, so
// v0.34.9 could ship a decoder that silently dropped a word out of every item
// description in the game with all fourteen gates green. This probe includes
// the REAL src/ff8_text_decode.cpp and drives it with the REAL namedic.bin.
//
// THE FIXTURE IS THE SHIPPED FILE. The 408 bytes below are main.fs entry 13
// (c:\ff8\data\eng\namedic.bin), LZS-decompressed, byte for byte. Nothing here
// is a reconstruction of what the table "should" look like -- if the layout
// this probe asserts were wrong, it would be wrong about the actual file.
//
// Build:  g++ -std=c++17 -Wall -Wextra -Isrc
//             -o tests_out/text_decode_compile
//             tests/text_decode_compile.cpp src/ff8_text_decode.cpp

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "ff8_text_decode.h"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { bad++; printf("  BAD: %s\n", what); } }

static void checkStr(const std::string& got, const char* want, const char* what)
{
    if (got != want) {
        bad++;
        printf("  BAD: %s\n        got  \"%s\"\n        want \"%s\"\n",
               what, got.c_str(), want);
    }
}

// ---------------------------------------------------------------------------
// namedic.bin -- main.fs entry 13, 408 bytes, decompressed.
//   u16 count; u16 offset[count]; packed NUL-terminated FF8 strings.
// ---------------------------------------------------------------------------
static const uint8_t NAMEDIC[408] = {
    0x20, 0x00, 0x42, 0x00, 0x4B, 0x00, 0x52, 0x00, 0x59, 0x00, 0x60, 0x00,
    0x67, 0x00, 0x6E, 0x00, 0x75, 0x00, 0x88, 0x00, 0x95, 0x00, 0xA3, 0x00,
    0xB1, 0x00, 0xBC, 0x00, 0xCA, 0x00, 0xD6, 0x00, 0xE4, 0x00, 0xF9, 0x00,
    0x08, 0x01, 0x1E, 0x01, 0x29, 0x01, 0x32, 0x01, 0x39, 0x01, 0x40, 0x01,
    0x48, 0x01, 0x4E, 0x01, 0x55, 0x01, 0x5F, 0x01, 0x66, 0x01, 0x6E, 0x01,
    0x77, 0x01, 0x88, 0x01, 0x8F, 0x01, 0x4B, 0x5F, 0x6A, 0x60, 0x5F, 0x62,
    0x67, 0x5F, 0x00, 0x49, 0x71, 0x72, 0x66, 0x5F, 0x70, 0x00, 0x46, 0x5F,
    0x6A, 0x5F, 0x6B, 0x60, 0x00, 0x48, 0x6D, 0x6A, 0x6A, 0x63, 0x72, 0x00,
    0x58, 0x67, 0x6B, 0x60, 0x63, 0x70, 0x00, 0x58, 0x70, 0x5F, 0x60, 0x67,
    0x5F, 0x00, 0x47, 0x63, 0x6C, 0x72, 0x70, 0x5F, 0x00, 0x4A, 0x67, 0x71,
    0x66, 0x63, 0x70, 0x6B, 0x5F, 0x6C, 0x71, 0x20, 0x4C, 0x6D, 0x70, 0x67,
    0x78, 0x6D, 0x6C, 0x00, 0x49, 0x5F, 0x71, 0x72, 0x20, 0x45, 0x61, 0x5F,
    0x62, 0x63, 0x6B, 0x77, 0x00, 0x48, 0x63, 0x71, 0x63, 0x70, 0x72, 0x20,
    0x54, 0x70, 0x67, 0x71, 0x6D, 0x6C, 0x00, 0x58, 0x70, 0x5F, 0x60, 0x67,
    0x5F, 0x20, 0x4B, 0x5F, 0x70, 0x62, 0x63, 0x6C, 0x00, 0x50, 0x73, 0x6C,
    0x5F, 0x70, 0x20, 0x46, 0x5F, 0x71, 0x63, 0x00, 0x57, 0x66, 0x73, 0x6B,
    0x67, 0x20, 0x5A, 0x67, 0x6A, 0x6A, 0x5F, 0x65, 0x63, 0x00, 0x48, 0x63,
    0x6A, 0x67, 0x6C, 0x65, 0x20, 0x47, 0x67, 0x72, 0x77, 0x00, 0x46, 0x5F,
    0x6A, 0x5F, 0x6B, 0x60, 0x20, 0x4B, 0x5F, 0x70, 0x62, 0x63, 0x6C, 0x00,
    0x49, 0x5F, 0x71, 0x72, 0x20, 0x45, 0x61, 0x5F, 0x62, 0x63, 0x6B, 0x77,
    0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00, 0x48, 0x6D, 0x6A,
    0x6A, 0x63, 0x72, 0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00,
    0x48, 0x63, 0x71, 0x63, 0x70, 0x72, 0x20, 0x54, 0x70, 0x67, 0x71, 0x6D,
    0x6C, 0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00, 0x50, 0x73,
    0x6C, 0x5F, 0x70, 0x20, 0x4B, 0x5F, 0x72, 0x63, 0x00, 0x56, 0x63, 0x71,
    0x72, 0x6D, 0x70, 0x63, 0x71, 0x00, 0x71, 0x72, 0x5F, 0x72, 0x73, 0x71,
    0x00, 0x6A, 0x63, 0x5F, 0x70, 0x6C, 0x71, 0x00, 0x5F, 0x60, 0x67, 0x6A,
    0x67, 0x72, 0x77, 0x00, 0x51, 0x5F, 0x65, 0x67, 0x61, 0x00, 0x56, 0x63,
    0x64, 0x67, 0x6C, 0x63, 0x00, 0x4E, 0x73, 0x6C, 0x61, 0x72, 0x67, 0x6D,
    0x6C, 0x71, 0x00, 0x56, 0x5F, 0x67, 0x71, 0x63, 0x71, 0x00, 0x61, 0x6D,
    0x6B, 0x6B, 0x5F, 0x6C, 0x62, 0x00, 0x51, 0x5F, 0x65, 0x5F, 0x78, 0x67,
    0x6C, 0x63, 0x00, 0x59, 0x6A, 0x72, 0x67, 0x6B, 0x63, 0x61, 0x67, 0x5F,
    0x20, 0x47, 0x5F, 0x71, 0x72, 0x6A, 0x63, 0x00, 0x4B, 0x5F, 0x70, 0x62,
    0x63, 0x6C, 0x00, 0x48, 0x63, 0x6A, 0x67, 0x6C, 0x65, 0x00, 0x00, 0x00,
};

// FF8 raw encoding, for writing fixtures the way the game stores them.
static uint8_t Enc(char c)
{
    if (c == ' ')                 return 0x20;
    if (c >= '0' && c <= '9')     return (uint8_t)(0x21 + (c - '0'));
    if (c >= 'A' && c <= 'Z')     return (uint8_t)(0x45 + (c - 'A'));
    if (c >= 'a' && c <= 'z')     return (uint8_t)(0x5F + (c - 'a'));
    switch (c) {
        case '%': return 0x2B; case '/': return 0x2C; case ':': return 0x2D;
        case '!': return 0x2E; case '?': return 0x2F; case '+': return 0x31;
        case '-': return 0x32; case '=': return 0x33; case '*': return 0x34;
        case '&': return 0x35; case '(': return 0x38; case ')': return 0x39;
        case '.': return 0x3B; case ',': return 0x3C; case '~': return 0x3D;
        case '\'': return 0x3A; case '"': return 0x3E; case '#': return 0x41;
        case '$': return 0x42; case '_': return 0x44;
        default:  return 0x20;
    }
}
static size_t Put(uint8_t* p, const char* s)
{ size_t i = 0; for (; s[i]; i++) p[i] = Enc(s[i]); p[i] = 0; return i; }

int main()
{
    printf("ff8_text_decode.cpp compiles\n");

    FF8TextDecode::SetWordTableBase(NAMEDIC);

    // --- the table's own layout ------------------------------------------
    check(NAMEDIC[0] == 32 && NAMEDIC[1] == 0, "namedic.bin declares 32 entries");
    {
        uint8_t w[64];
        check(FF8TextDecode::ResolveWord(0x0E, 0x20, w, sizeof(w)) == 8,
              "entry 0 is eight bytes long");
        checkStr(FF8TextDecode::Decode(w, sizeof(w)), "Galbadia", "entry 0 is Galbadia");

        check(FF8TextDecode::ResolveWord(0x0E, 0x23, w, sizeof(w)) > 0,
              "entry 3 resolves");
        checkStr(FF8TextDecode::Decode(w, sizeof(w)), "Dollet",
                 "entry 3 is Dollet -- the #78 field-dialog case, same table");

        checkStr((FF8TextDecode::ResolveWord(0x0E, 0x35, w, sizeof(w)),
                  FF8TextDecode::Decode(w, sizeof(w))), "learns", "entry 0x15 is learns");
        checkStr((FF8TextDecode::ResolveWord(0x0E, 0x36, w, sizeof(w)),
                  FF8TextDecode::Decode(w, sizeof(w))), "ability", "entry 0x16 is ability");
        checkStr((FF8TextDecode::ResolveWord(0x0E, 0x37, w, sizeof(w)),
                  FF8TextDecode::Decode(w, sizeof(w))), "Magic", "entry 0x17 is Magic");

        // Out of range and wrong-code both emit nothing, exactly as the engine
        // does (0x004B8CF4 / the 0x0E..0x0F group check).
        check(FF8TextDecode::ResolveWord(0x0E, 0x40, w, sizeof(w)) == 0,
              "param past the entry count emits nothing");
        check(FF8TextDecode::ResolveWord(0x0F, 0x20, w, sizeof(w)) == 0,
              "group 1 is past the count in the English file");
        check(FF8TextDecode::ResolveWord(0x0D, 0x20, w, sizeof(w)) == 0,
              "0x0D is not a word code");
    }

    // --- THE BAT BYTES ----------------------------------------------------
    // v0.34.9's [SHOP-TEXT] line, verbatim. It spoke as "GF".
    {
        const uint8_t magicScroll[] = {
            0xF0, 0x20, 0x0E, 0x35, 0x20, 0x0E, 0x37, 0x20, 0x0E, 0x36, 0x00
        };
        int dropped = -1;
        std::string s = FF8TextDecode::Decode(magicScroll, sizeof(magicScroll), &dropped);
        checkStr(s, "GF learns Magic ability", "the Magic Scroll description, whole");
        check(dropped == 0, "and nothing reported as lost");
    }

    // The other two fragments from the same BAT, rebuilt from kernel.bin.
    {
        // "Restores 1000 HP to GF"  ->  0E 33 = "Restores"
        const uint8_t potion[] = {
            0x0E, 0x33, 0x20, 0x22, 0x21, 0x21, 0x21, 0x20, 0xED, 0x20,
            0x72, 0x6D, 0x20, 0xF0, 0x00
        };
        int dropped = -1;
        checkStr(FF8TextDecode::Decode(potion, sizeof(potion), &dropped),
                 "Restores 1000 HP to GF", "v0.34.8's \" 1000 HP to GF\" fragment");
        check(dropped == 0, "nothing lost");
    }

    // --- what happens when the table is NOT there --------------------------
    // The engine emits nothing; so do we, and we SAY that we did. This is the
    // case that matters: a fragment must never pass for a whole sentence.
    {
        FF8TextDecode::SetWordTableBase(nullptr);
        const uint8_t magicScroll[] = {
            0xF0, 0x20, 0x0E, 0x35, 0x20, 0x0E, 0x37, 0x20, 0x0E, 0x36, 0x00
        };
        int dropped = 0;
        std::string s = FF8TextDecode::Decode(magicScroll, sizeof(magicScroll), &dropped);
        checkStr(s, "GF   ", "no table -> the engine's own output: nothing");
        check(dropped == 3, "and all three lost words are counted");
        FF8TextDecode::SetWordTableBase(NAMEDIC);
    }

    // --- the 57 bytes that used to vanish ---------------------------------
    // Derived table: glyph = byte - 0x20. 0x96 is an accented 'e', 0xA9/0xAA
    // are brackets, 0xB5 a semicolon. Every one of these was dropped before.
    {
        const uint8_t accents[] = { 0x47, 0x5f, 0x64, 0x96, 0x00 };  // C a f e-acute
        int dropped = -1;
        checkStr(FF8TextDecode::Decode(accents, sizeof(accents), &dropped), "Cafe",
                 "0x96 resolves to a letter instead of vanishing");
        check(dropped == 0, "so nothing is reported lost");

        const uint8_t brackets[] = { 0xA9, 0x4D, 0xAA, 0xB5, 0x00 };  // "[I];"
        checkStr(FF8TextDecode::Decode(brackets, sizeof(brackets)), "[I];",
                 "brackets and semicolon resolve");
    }

    // --- ordinary text is unchanged ---------------------------------------
    {
        uint8_t buf[128];
        Put(buf, "Removes KO status");
        int dropped = -1;
        checkStr(FF8TextDecode::Decode(buf, sizeof(buf), &dropped),
                 "Removes KO status", "plain text round-trips");
        check(dropped == 0, "and reports no loss");

        Put(buf, "Don't!");
        checkStr(FF8TextDecode::Decode(buf, sizeof(buf)), "Don't!",
                 "apostrophe override survives the derived table");

        const uint8_t ell[] = { 0x30, 0x00 };
        checkStr(FF8TextDecode::Decode(ell, sizeof(ell)), "...", "0x30 is still an ellipsis");
    }

    // --- 0x0D consumes its parameter (v0.35.0) ----------------------------
    // Before this, 0x0D emitted nothing AND consumed nothing, so the parameter
    // byte leaked out as a stray character in the middle of a sentence.
    {
        const uint8_t leak[] = { 0x0D, 0x45, 0x4D, 0x72, 0x00 };  // 0x0D 'A', then "It"
        int dropped = 0;
        checkStr(FF8TextDecode::Decode(leak, sizeof(leak), &dropped), "It",
                 "0x0D swallows its param instead of leaking an 'A'");
        check(dropped == 1, "and the unresolved name is counted as lost");
    }

    // --- unmapped bytes are still reported --------------------------------
    {
        const uint8_t blank[] = { 0x4D, 0x72, 0xB0, 0x00 };   // 0xB0 -> blank cell
        int dropped = 0;
        checkStr(FF8TextDecode::Decode(blank, sizeof(blank), &dropped), "It",
                 "a genuinely blank glyph still produces nothing");
        check(dropped == 1, "but it is counted, so a caller can say so");
    }

    // --- DecodeLines still splits, and expands ----------------------------
    {
        const uint8_t two[] = { 0x0E, 0x22, 0x02, 0x0E, 0x21, 0x00 };
        std::vector<std::string> lines = FF8TextDecode::DecodeLines(two, sizeof(two));
        check(lines.size() == 2, "two lines");
        if (lines.size() == 2) {
            checkStr(lines[0], "Balamb", "line 1 expanded");
            checkStr(lines[1], "Esthar", "line 2 expanded");
        }
    }

    // --- the nameable names (v0.38.0, #97) ---------------------------------
    // Aaron's Rinoa limit read "Name 40" instead of Angelo. 0x40 is not an index
    // into the party table at all -- the engine's ladder at 0x004B8DAC sends it
    // to a savemap pointer, and so do 0x50 and 0x60. The offsets below are the
    // five addresses that branch names, verified against Aaron's own save.
    {
        static uint8_t SAVEMAP[0xB00] = {0};
        auto put = [](int off, const char* s) {
            for (int i = 0; s[i]; i++) SAVEMAP[off + i] = Enc(s[i]);
        };
        put(0x14,  "Squall");
        put(0x20,  "Rinoa");
        put(0x2C,  "Angelo");
        put(0x38,  "Boko");
        put(0xAF8, "Griever");
        FF8TextDecode::SetNameTableBase(SAVEMAP);

        const uint8_t angelo[]  = { 0x03, 0x40, 0x00 };
        const uint8_t griever[] = { 0x03, 0x50, 0x00 };
        const uint8_t boko[]    = { 0x03, 0x60, 0x00 };
        const uint8_t squall[]  = { 0x03, 0x30, 0x00 };
        const uint8_t rinoa[]   = { 0x03, 0x34, 0x00 };
        int dropped = -1;
        checkStr(FF8TextDecode::Decode(angelo, sizeof(angelo), &dropped), "Angelo",
                 "**0x03 0x40 is Angelo**, not \"[Name40]\"");
        check(dropped == 0, "and nothing is reported lost");
        checkStr(FF8TextDecode::Decode(griever, sizeof(griever)), "Griever", "0x50 is Griever");
        checkStr(FF8TextDecode::Decode(boko, sizeof(boko)), "Boko", "0x60 is Boko");
        checkStr(FF8TextDecode::Decode(squall, sizeof(squall)), "Squall",
                 "0x30 comes from the savemap too -- Squall is renameable");
        checkStr(FF8TextDecode::Decode(rinoa, sizeof(rinoa)), "Rinoa",
                 "and so is Rinoa at 0x34");

        // A RENAMED pet must read as the player named it. This is the whole
        // reason these five are read live instead of hardcoded.
        memset(SAVEMAP + 0x2C, 0, 12);
        put(0x2C, "Rex");
        checkStr(FF8TextDecode::Decode(angelo, sizeof(angelo)), "Rex",
                 "a renamed Angelo reads as the player named it");
        memset(SAVEMAP + 0x2C, 0, 12);
        put(0x2C, "Angelo");

        // The ten that are NOT renameable still come from the built-in table.
        const uint8_t zell[] = { 0x03, 0x31, 0x00 };
        checkStr(FF8TextDecode::Decode(zell, sizeof(zell)), "Zell",
                 "a kernel-table name still resolves from the built-in list");

        // And an id that is neither says so, and counts itself as lost.
        const uint8_t bogus[] = { 0x03, 0x7F, 0x00 };
        dropped = 0;
        checkStr(FF8TextDecode::Decode(bogus, sizeof(bogus), &dropped), "[Name7F]",
                 "an id in no branch of the ladder is still marked, not invented");
        check(dropped == 1, "and counted, so a caller knows the line is incomplete");

        // With no savemap at all, the built-in table still covers the ten.
        FF8TextDecode::SetNameTableBase(nullptr);
        checkStr(FF8TextDecode::Decode(zell, sizeof(zell)), "Zell",
                 "no savemap -> the built-in list still answers for Zell");
        FF8TextDecode::SetNameTableBase(SAVEMAP);
    }

    // v0.40.0 (#101): the emphasis pair. These bytes wrap a highlighted term
    // -- `{AE}Save Point{AF}` -- draw no glyph, and are blank in the Deling
    // grid. Before this they counted as two lost bytes on every such string,
    // which is exactly the signal a caller uses to decide it is speaking a
    // fragment.
    {
        // The first twelve bytes of gproof2.msd message 1, verbatim:
        // "{AE}Save Point{AF}". Typed out by hand the first time, which put an
        // 'L' where the 'P' belongs and made the probe fail for the wrong
        // reason -- the bytes now come from the archive, like every other
        // fixture in this file.
        const unsigned char emph[] = { 0xAE, 0x57, 0x5F, 0x74, 0x63, 0x20, 0x54,
                                       0x6D, 0x67, 0x6C, 0x72, 0xAF, 0x00 };
        int dropped = -1;
        std::string out = FF8TextDecode::Decode(emph, sizeof(emph), &dropped);
        if (out != "Save Point") {
            printf("  BAD: emphasis pair -- got \"%s\", want \"Save Point\"\n", out.c_str());
            bad++;
        }
        if (dropped != 0) {
            printf("  BAD: emphasis pair counted %d dropped byte(s), want 0\n", dropped);
            bad++;
        }
    }

    printf(bad ? "FAILED: %d\n" : "OK\n", bad);
    return bad ? 1 : 0;
}
