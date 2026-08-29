// dialog_ellipsis.inl -- the game's silence beat, made audible.
//
// Pure: <string> and nothing else. Included from field_dialog_scan.inl, and
// compiled standalone by tests/dialog_ellipsis_compile.cpp, whose fixture is
// every distinct dots-only message in the 883 field message files.

// v0.50.0 (#108): **A WINDOW THAT IS NOTHING BUT DOTS IS THE GAME'S SILENCE
// BEAT, AND IT REACHED THE PLAYER AS NOTHING AT ALL.**
//
// 2026-08-21, bghoke_2: Squall says *"Rinoa... Call my name."* and the reply
// window holds `(......)`. The log shows `REJECTED garbled` -- eight characters,
// zero letters, so IsGarbledText's `letterPct < 30` rule fired, which is the
// right rule for a stale buffer and the wrong one for a deliberate pause.
//
// The shorter ones were no better off. `...` is three characters, under
// IsGarbledText's `len < 8` floor, so it was passed straight through to the
// screen reader -- which reads punctuation-only text as silence at default
// settings. Either way the player got a window with no announcement and no
// reason to know a confirm press was waiting.
//
// Across the 883 field message files there are 50 such messages: 41 bare `...`,
// six `(......)`, and three longer runs. That is the whole population, so this
// rewrite is bounded by measurement rather than by guess.
//
// A line with ANY letter or digit is not this -- "Well..." keeps its own words.
static bool IsEllipsisOnly(const std::string& text)
{
    int dots = 0;
    for (size_t i = 0; i < text.size(); i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '.') { dots++; continue; }
        if (c == '(' || c == ')' || c == ' ' || c == '\t' ||
            c == '\r' || c == '\n') continue;
        return false;
    }
    return dots >= 3;
}

// The spoken copy only -- the dedup keys stay the original text, the same rule
// ApplyTrainCodeKeyFix follows.
static void ApplyEllipsisFix(std::string& text)
{
    if (IsEllipsisOnly(text)) text = "Ellipsis.";
}
