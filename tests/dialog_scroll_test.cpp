// dialog_scroll_test.cpp -- the Ragnarok terminal, and why it read itself four
// times.
//
//   g++ -std=c++17 -O0 -Isrc -Itests/winshim -o dialog_scroll_test \
//       tests/dialog_scroll_test.cpp
//
// Aaron, 2026-08-24, on the passenger-compartment terminal: *"it seemed to
// repeat itself, like it was loading parts of the message repeatedly."*
//
// It did, four times, and the two containment tests already in the dialog code
// could not see why. That terminal's message is longer than its box, so FF8
// scrolls it and the mod re-reads the window on every scroll -- but each read is
// also cut off at the decoder's 512-character limit, so consecutive reads
// OVERLAP without either containing the other. Both tests need containment.
// Both fail. It speaks again.
//
// The strings below are the real ones, from Logs/ff8_dialog.log at 23:11:15,
// 23:11:16 and 23:11:17, trimmed only in the middle to keep this file readable.

#include <cstdio>
#include <cstring>
#include <string>

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }
static void checkStr(const std::string& got, const std::string& want, const char* what)
{
    if (got != want) {
        std::printf("  BAD: %s\n        got  \"%s\"\n        want \"%s\"\n",
                    what, got.c_str(), want.c_str());
        bad++;
    }
}

// ---- the REAL code under test, and nothing else ---------------------------
#include "dialog_scroll_pure.inl"

// ---------------------------------------------------------------------------
// A length is not a buffer size (v0.106.0)
// ---------------------------------------------------------------------------
static void TestCopyBytesFor()
{
    // THE BUG, AS ARITHMETIC. `DialogPageBytes` says "Blizzara" is eight bytes;
    // SafeCopyEngineText given eight bytes of buffer copies seven and writes a
    // terminator, and the player hears "Blizzar". Nine is the answer.
    check(DialogCopyBytesFor(8, 513) == 9, "eight characters need nine bytes");
    check(DialogCopyBytesFor(0, 513) == 1, "an empty page still needs its terminator");
    check(DialogCopyBytesFor(1, 513) == 2, "and one character needs two");

    // The property that matters, across the whole range a page can be: the
    // buffer asked for is always BIGGER than the text, or a character is lost.
    // Kills the mutant that hands the length straight back -- which is exactly
    // what v0.71.0 did.
    for (size_t n = 0; n < 512; n++)
        check(DialogCopyBytesFor(n, 513) > n,
              "the copy is always given room for the terminator");

    // ...and never bigger than the buffer, or SafeCopyEngineText would run off
    // the end of it.
    check(DialogCopyBytesFor(512, 513) == 513, "a full page fills the buffer exactly");
    check(DialogCopyBytesFor(9999, 513) == 513, "an over-long page is clamped to the buffer");
    check(DialogCopyBytesFor(512, 512) == 512, "and clamped when the buffer is the old size");
    check(DialogCopyBytesFor(5, 0) == 0, "a zero buffer asks for nothing");

    // The two halves of the contract, together: a page measured by
    // DialogPageBytes and copied with DialogCopyBytesFor keeps every character.
    const uint8_t blizzara[] = { 0x42,0x6C,0x69,0x7A,0x7A,0x61,0x72,0x61, 0x00, 0x00 };
    const size_t len = DialogPageBytes(blizzara, sizeof blizzara);
    check(len == 8, "the page is the eight bytes before the terminator");
    check(DialogCopyBytesFor(len, 513) == len + 1,
          "and the copy that preserves them is one longer");
}

int main()
{
    TestCopyBytesFor();
    std::printf("dialog_scroll_test\n");

    // The real terminal, as three overlapping reads. Read 1 is cut mid-word at
    // the decoder's limit -- that truncation is what defeated the old tests.
    const std::string read1 =
        "The 8 monsters work together to maintain their colony. "
        "They seem immortal, but they are not. Killing them in a certain order "
        "prevents them from reviving one another. We have confirmed that i";
    const std::string read2 =
        "They seem immortal, but they are not. Killing them in a certain order "
        "prevents them from reviving one another. We have confirmed that is all "
        "the information we have. Good luck";
    const std::string read3 =
        "prevents them from reviving one another. We have confirmed that is all "
        "the information we have. Good luck";

    // Neither read contains the other, which is exactly why this needed a new
    // rule rather than a wider one.
    check(read1.find(read2) == std::string::npos &&
          read2.find(read1) == std::string::npos,
          "**neither read contains the other** -- both are truncated, so every "
          "containment test in the world fails on them");

    // Read 1 is new: all of it.
    checkStr(ContinuationTail("", read1), read1, "the first read is entirely new");

    // Read 2 is the same passage scrolled. Only the part read 1 was cut off
    // before may be spoken -- and it starts at a word boundary, not in the
    // middle of "is".
    const std::string tail2 = ContinuationTail(read1, read2);
    check(tail2 != read2,
          "**a scroll does not repeat the passage** -- this is the bug he heard");
    check(!tail2.empty(),
          "**but it is not silent either** -- read 1 was cut mid-sentence, and "
          "suppressing read 2 outright would lose the end of the message for good");
    check(tail2.rfind("is all the information we have. Good luck") != std::string::npos,
          "it says the part that was cut off");
    check(tail2.compare(0, 2, "is") == 0,
          "**and it starts at a word boundary** -- read 1 stopped inside \"is\", so "
          "the tail finishes the word rather than beginning \"s all the...\"");

    // Read 3 adds nothing at all.
    checkStr(ContinuationTail(read1 + tail2, read3), "",
             "**a scroll that adds nothing says nothing**");

    // An unrelated message is not a scroll, however long it is.
    const std::string other =
        "Rinoa \"Umm... So basically, we have to kill them in pairs that have the "
        "same colors, right?\" And then some more words to get past the length floor.";
    checkStr(ContinuationTail(read1, other), other,
             "**a different message is spoken in full** -- the rule must not "
             "swallow the next thing anyone says");

    // Short lines are never treated as scrolls: they share openings all the time
    // ("Yes"/"Yes, please"), and a menu that went quiet would be far worse than
    // a terminal that repeated itself.
    checkStr(ContinuationTail("Yes", "Yes, please"), "Yes, please",
             "short strings are exempt");
    checkStr(ContinuationTail("Open the gate?", "Open the gate? Yes No"),
             "Open the gate? Yes No", "and so are ordinary prompts");

    // The boundary itself: both sides have to be longer than the key, or the
    // match is not distinctive enough to trust.
    {
        const std::string a(DLG_OVERLAP_KEY, 'x');
        const std::string b(DLG_OVERLAP_KEY, 'x');
        checkStr(ContinuationTail(a, b), b,
                 "exactly the key length is not long enough to call a scroll");
    }

    // =======================================================================
    // ONE PAGE, NOT THE REST OF THE REPORT (v0.70.0)
    // =======================================================================
    // v0.69.0 treated this as a SCROLL and suppressed the overlap. It is not a
    // scroll: ff8_win_obj + 0x08 is the start of the CURRENT PAGE inside the
    // message, and the engine advances it on every Confirm. Decoding from there
    // to the terminator gives this page AND EVERY PAGE AFTER IT, so a five-page
    // report was read out from page one, then again from page two, then again
    // from page three -- overlapping recitations that no containment test can
    // tidy up, because each really is new text.
    //
    // The engine ends a page at code 0x01 and waits for input. So does the
    // decode now, and what is spoken is what is on the screen.
    {
        // "AB" 0x01 "CD" 0x00 -- two pages.
        const uint8_t twoPages[] = { 0x45,0x46, 0x01, 0x47,0x48, 0x00 };
        check(DialogPageBytes(twoPages, sizeof twoPages) == 2,
              "**a page stops at the page break** -- not at the end of the report");

        // The second page, as the engine hands it over on the next Confirm.
        check(DialogPageBytes(twoPages + 3, sizeof twoPages - 3) == 2,
              "and the next page is measured from where the engine moved the pointer");

        // No page break at all: the whole string, stopping at the terminator.
        const uint8_t onePage[] = { 0x45,0x46,0x47, 0x00, 0x48 };
        check(DialogPageBytes(onePage, sizeof onePage) == 3,
              "a single-page message is not truncated");

        // A page break in the very first byte is an empty page, and the caller
        // has to be able to tell -- speaking "" would be a silent Confirm that
        // looks like a dropped line.
        const uint8_t empty[] = { 0x01, 0x45, 0x00 };
        check(DialogPageBytes(empty, sizeof empty) == 0, "an empty page measures zero");

        // Neither marker inside the buffer: everything we were given, and no
        // read past the end.
        const uint8_t nostop[] = { 0x45,0x46,0x47 };
        check(DialogPageBytes(nostop, sizeof nostop) == 3,
              "**and a buffer with no terminator stops at the buffer** -- this runs "
              "against engine memory");
        check(DialogPageBytes(nullptr, 8) == 0, "a null buffer measures nothing");

        // 0x02 is a LINE break and must not end the page: the terminal's third
        // screenful is four lines and would have lost three of them.
        const uint8_t lines[] = { 0x45, 0x02, 0x46, 0x02, 0x47, 0x00 };
        check(DialogPageBytes(lines, sizeof lines) == 5,
              "**a line break is not a page break** -- FF8's terminal report has "
              "four-line pages and cutting at 0x02 would speak a quarter of one");
    }

    std::printf(bad ? "dialog_scroll_test: FAILED (%d bad)\n"
                    : "dialog_scroll_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
