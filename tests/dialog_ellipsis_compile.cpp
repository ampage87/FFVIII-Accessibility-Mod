// dialog_ellipsis_compile -- the dots-only dialogue window, checked against the
// whole population of them in the game.
//
// WHY THIS EXISTS
// ---------------
// 2026-08-21, field bghoke_2: Squall says "Rinoa... Call my name." and the
// reply window holds `(......)`. ff8_dialog.log:
//
//     [RAMESW] win[1] REJECTED garbled: "(......)"
//
// Eight characters, no letters, so IsGarbledText's `letterPct < 30` rule fired.
// That rule is right for a stale tutorial buffer and wrong for a deliberate
// pause. The shorter form was no better off: `...` is three characters, under
// the `len < 8` floor, so it went to the screen reader unchanged -- and a
// screen reader reads punctuation-only text as silence. Either way a dialogue
// window arrived with no announcement.
//
// THE FIXTURE IS THE POPULATION, NOT A SAMPLE. Every message in the 883 field
// .msd files was decoded through the sysfnt glyph table (0x10 = "...", 0x18/
// 0x19 = the parentheses) and every one that is nothing but dots, parens and
// spaces is listed below with its count. Forty-nine messages, five distinct
// forms. Nothing here is invented, and nothing in the game is left out.
#include <cstdio>
#include <cstring>
#include <string>

#include "dialog_ellipsis.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

// Every distinct dots-only message in the field text, with how many times it
// occurs. Extracted 2026-08-21 from allfx/*/*.msd.
static const struct { const char* text; int count; } POPULATION[] = {
    { "...", 40 },
    { "(......)", 6 },
    { "(.........)", 1 },
    { "(............)", 1 },
    { "..................", 1 },
};

// Real dialogue that must keep every word it has. The first is the line the
// `(......)` was a reply to.
static const char* KEEPS_ITS_WORDS[] = {
    "Squall \"Rinoa... Call my name.\"",
    "Well...",
    "...I don't know.",
    "Laguna \"Why the heck do I have to  do this!?\"",
    "......huh?",
    ",e 3in*retone3 e~HP~B:All08E%~!/",       // the canonical garbage sample
};

int main()
{
    int total = 0;
    for (size_t i = 0; i < sizeof(POPULATION) / sizeof(POPULATION[0]); i++) {
        const std::string t = POPULATION[i].text;
        total += POPULATION[i].count;
        check(IsEllipsisOnly(t), "every dots-only message in the game is recognised");
        std::string spoken = t;
        ApplyEllipsisFix(spoken);
        check(spoken == "Ellipsis.",
              "**and every one of them becomes a word the reader will say** -- "
              "punctuation-only text is silence at default settings");
    }
    check(total == 49,
          "the fixture is the whole population: 49 messages, 5 distinct forms");

    for (size_t i = 0; i < sizeof(KEEPS_ITS_WORDS) / sizeof(KEEPS_ITS_WORDS[0]); i++) {
        const std::string t = KEEPS_ITS_WORDS[i];
        check(!IsEllipsisOnly(t),
              "**a line with any letter or digit is not a silence beat** -- "
              "\"Well...\" and \"...I don't know.\" keep their own words");
        std::string spoken = t;
        ApplyEllipsisFix(spoken);
        check(spoken == t, "and are handed on untouched");
    }

    // The floor: two dots is an abbreviation artefact, not a beat, and an empty
    // window is not one either.
    check(!IsEllipsisOnly(""),   "an empty window is not a silence beat");
    check(!IsEllipsisOnly(".."), "and neither is a two-dot fragment");
    check(!IsEllipsisOnly("()"), "nor a pair of parentheses with nothing in them");
    check(IsEllipsisOnly("..."), "three is the floor, which is the game's own short form");
    check(IsEllipsisOnly("( ... )"),
          "spaces inside the parentheses do not change what it is");

    std::printf(bad ? "dialog_ellipsis_compile: FAILED (%d bad)\n"
                    : "dialog_ellipsis_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
