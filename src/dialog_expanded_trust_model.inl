// dialog_expanded_trust_model.inl -- AN EXPANDED MESSAGE IS NOT STALE GARBAGE
//
// v0.117.0 (#centra). Aaron: "the only bug I noticed was the code not reading
// out when I put both eyes in the top statue. When you do that it displays the
// code visually but isn't reading it."
//
// The mod built the sentence correctly and then threw it away:
//
//   [VAR-EXPAND] 5 insert(s): "Code:.  '. 0'. 1'. 2'. 3" -> "Code:9'8'9'3'9"
//   [AMESW] win[0] REJECTED garbled: "Code:9'8'9'3'9"
//
// v0.116.0's numeric-insert fix worked -- those are his five digits -- and
// IsGarbledText then killed it on `letterPct < 30`. Four letters in fourteen
// characters is 28%. **A five-digit code is mostly digits by definition**, so
// that filter will reject every code, every quantity readout and every score
// the engine ever builds out of numeric inserts. Correcting 0x3A to a space
// does not save it either: "Code: 9 8 9 3 9" is 26%.
//
// WHY THE FILTER EXISTS, AND WHY THIS DOES NOT WEAKEN IT. v0.17.8.1 added
// IsGarbledText for a real bug: after a tutorial tore down, the engine left its
// overlay bytes in win[0]'s text region and the poll decoded them as dialog
// (",e 3in*retone3 e~HP~B:All08E%~!/..."). Every heuristic in it -- letter
// ratio, punctuation density, letter/digit transitions -- is a proxy for one
// question: **are these bytes a message, or are they leftovers?**
//
// A successful variable expansion answers that question directly, and better
// than any ratio can. To report N inserts the expander must have found N
// well-formed `0x04 + param` control codes, each param inside a range the
// engine's own resolver accepts (sub_4B8E40: 0x20-0x27, 0x30-0x37, 0x40-0x47),
// with ordinary literal text between them. Stale buffer leftovers do not
// contain five valid numeric-insert control codes in a row. The proxy and the
// direct evidence disagree here, so the direct evidence wins.
//
// The exemption is deliberately narrow: it applies only when the expansion
// actually substituted something. A message the expander looked at and changed
// nothing in still faces the full filter, which is every message the Fire
// Cavern bug was about.

static bool DetExpansionIsTrusted(int insertCount)
{
    return insertCount > 0;
}

// The one-line policy the call sites share, so the two of them cannot drift.
static bool DetShouldRunGarbleFilter(int insertCount)
{
    return !DetExpansionIsTrusted(insertCount);
}
