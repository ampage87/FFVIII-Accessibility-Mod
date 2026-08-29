// dialog_scroll_pure.inl -- pure string arithmetic for FF8's scrolling windows.
//
// Included by src/field_dialog_helpers.inl (the shipping path) and by
// tests/dialog_scroll_test.cpp (the probe). No engine surface, no Windows, no
// logging -- just <string>. Same split field_overlay_pure.inl uses.

#pragma once
#include <string>
#include <cstddef>
#include <cstdint>

// How many bytes of this buffer belong to the CURRENT PAGE.
//
// FF8 ends a page with code 0x01 and parks the window waiting for the player;
// 0x00 ends the string. Everything between is one screenful. Pure, and named,
// because the bug it fixes was invisible until somebody looked at a screenshot
// and counted the pages -- see the block comment at its call site in
// field_dialog_show_dialog.inl.
static size_t DialogPageBytes(const uint8_t* buf, size_t n)
{
    if (!buf) return 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == 0x00 || buf[i] == 0x01) return i;
    return n;
}


// A LENGTH IS NOT A BUFFER SIZE, AND v0.71.0 PASSED ONE FOR THE OTHER (v0.106.0).
//
// Aaron: *"battle text is sometimes getting cut off. For example when Curaga was
// cast the announcement didn't say Curaga in full."* The 2026-08-26 dialog log
// is a column of them -- `Blizzar`, `Curag`, `Sca`, `Booy`, `Heel Dro`,
// `Meteor Strik`, `Burning Rav`, `Breat`, `10,000 Needle`, `Diamond Dus` --
// every single one exactly ONE CHARACTER short.
//
// `DialogPageBytes` above returns a LENGTH: the number of text bytes before the
// terminator. `SafeCopyEngineText` takes a BUFFER SIZE and reserves one byte of
// it for the NUL it writes, so it copies at most `outSize - 1` characters.
// v0.71.0's DecodeDialogPage handed the length straight through as the size, so
// every page decoded through it lost its last character.
//
// WHY IT LOOKED LIKE A BATTLE BUG. In field dialogue the page ends at a control
// byte -- a newline or a page break -- so the byte that went missing was the
// control byte, which decodes to nothing anyway. In battle the popups are bare
// single-line strings that end at their own 0x00, so the byte that went missing
// was the last letter. And the ones that survived are the ones FF8 pads: the
// buffer holds "Escape " so the page is seven bytes, six survive, and
// TrimDecoded takes the trailing space off anyway. Same off-by-one throughout;
// only the padding decided whether anyone could hear it.
static size_t DialogCopyBytesFor(size_t textBytes, size_t bufCap)
{
    // No zero-buffer guard: the clamp already answers 0 for it, and a mutant
    // that removed such a guard could not be killed. A second check for a case
    // the first one covers is a check nothing tests.
    const size_t want = textBytes + 1;      // + the terminator SafeCopy writes
    return (want < bufCap) ? want : bufCap;
}

// Aaron, on the Ragnarok passenger-compartment terminal: *"it seemed to repeat
// itself, like it was loading parts of the message repeatedly."* It did, four
// times, and the containment tests in field_dialog_helpers.inl could not see why.
//
// That terminal's message is longer than its box, so FF8 SCROLLS it, and the mod
// re-reads the window on every scroll. The reads overlap but neither contains
// the other, because each one is cut off at the decoder's output limit:
//
//   read 1  "The 8 monsters work together ... We have confirmed that i"
//   read 2  "They seem immortal, but they are not ... That is all ... Good luck"
//
// Read 2 is not a suffix of read 1 (it runs past where read 1 was cut) and read
// 1 does not contain it (same reason). Both containment tests fail, so it spoke
// again. And again, and again.
//
// THE SIGNATURE OF A SCROLL is that the new read STARTS somewhere inside the old
// one. That is what this looks for, and it does something better than
// suppressing: it works out how much of the new read is genuinely new and hands
// back only that. So the first read speaks the passage, the next speaks the tail
// the truncation cut off, and the rest say nothing -- which is the whole message
// once, in order, instead of four overlapping copies or (if this merely
// suppressed) a message with its ending missing.
//
// Only for long passages. OVERLAP_KEY characters have to match, and both strings
// have to be longer than that, so a pair of short lines that happen to share an
// opening cannot be mistaken for a scroll.
static const size_t DLG_OVERLAP_KEY = 40;

// "" when the new read adds nothing. Otherwise the part not spoken yet, backed
// up to a word boundary so a read that was cut mid-word is finished rather than
// resumed from the middle of it.
static std::string ContinuationTail(const std::string& spoken, const std::string& fresh)
{
    if (spoken.length() <= DLG_OVERLAP_KEY || fresh.length() <= DLG_OVERLAP_KEY)
        return fresh;
    const size_t at = spoken.find(fresh.substr(0, DLG_OVERLAP_KEY));
    if (at == std::string::npos) return fresh;      // unrelated: a new message

    const size_t covered = spoken.length() - at;    // how much of `fresh` we said
    if (covered >= fresh.length()) return "";       // all of it, already
    size_t start = covered;
    while (start > 0 && fresh[start - 1] != ' ' && fresh[start - 1] != '\n') start--;
    return fresh.substr(start);
}