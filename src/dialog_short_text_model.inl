// dialog_short_text_model.inl -- WHAT THE LENGTH GATE IS ALLOWED TO THROW AWAY
//
// v0.107.0 (#megaflare).
//
// `MIN_TEXT_LENGTH = 3` has stood in field_dialog_state.inl since v04.x with
// its reason written next to it: "Skip junk like ',' or 'C0'". That reason is
// a FIELD reason. The field window array is polled every frame whether or not
// it holds a message, and a one- or two-character decode out of an idle window
// really is an artefact.
//
// In BATTLE the same array holds nothing but deliberate feedback -- "Draw",
// "Mega Flare", "Received 2 Full-lifes!", "Game over" -- and the gate has been
// quietly eating anything shorter than three characters. Worse, in
// field_dialog_show_dialog.inl it returns BEFORE the [SHOW_DIALOG-TEXT] log
// line, so a dropped short text left no trace whatsoever. The 2026-08-26
// Bahamut log is the proof: ten seconds pass between
//
//     [16:50:38] ... text="Mega Flare"
//     [16:50:48] ... Zell takes 3862 damage. Defeated.
//
// and in that gap the log holds nothing but the two once-a-second heartbeats.
// Aaron, on the fight that killed the party: "There is a visible countdown
// that appears as Bahamut prepares to do his Mega Flare... It is possible the
// mod did perceive it but it was overridden in the heat of battle. It is just
// displayed as a single digit and very briefly."
//
// A gate that discards without logging cannot answer that question either way,
// which is the actual defect fixed here.
//
// THE RULE. In battle, a decoded text made ENTIRELY of digits passes at any
// length. Everything else keeps the old rule in every mode. Digits are not the
// shape of the artefacts the gate was written for -- those are punctuation and
// letter pairs -- and they are exactly the shape of a countdown. Widening the
// gate to "any short text in battle" was the alternative and was rejected: it
// would have let every stray fragment through in the one place where a stray
// utterance costs the player the turn they were listening for.
//
// `minLen` is passed in rather than named in here so this file and
// MIN_TEXT_LENGTH cannot drift apart.

static const unsigned DLG_MODE_BATTLE = 3u;

// Every character '0'..'9', and at least one of them. An empty string is not
// "all digits" -- there is nothing there to say.
static bool DlgTextIsAllDigits(const char* s)
{
    if (s == nullptr || s[0] == '\0') return false;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

// The gate. True means the caller should go on and speak/log the text.
static bool DlgTextPassesLengthGate(const char* decoded, size_t len,
                                    unsigned mode, int minLen)
{
    if (decoded == nullptr || len == 0) return false;
    if ((int)len >= minLen) return true;
    return (mode == DLG_MODE_BATTLE) && DlgTextIsAllDigits(decoded);
}

// True when the gate rejected something that was not empty -- a real decoded
// string went in the bin and nobody heard about it. That, and only that, is
// worth a diagnostic line; an empty decode is the resting state of an unused
// window and would flood the log.
static bool DlgShortDropWorthLogging(const char* decoded, size_t len,
                                     unsigned mode, int minLen)
{
    if (decoded == nullptr || len == 0) return false;
    return !DlgTextPassesLengthGate(decoded, len, mode, minLen);
}

// The diagnostic is uncapped per window but capped overall, because the field
// path can produce these at frame rate on a bad decode and this build is meant
// to survive a whole play session.
static const unsigned DLG_SHORT_LOG_CAP = 300u;

static bool DlgShortLogAllowed(unsigned alreadyLogged)
{
    return alreadyLogged < DLG_SHORT_LOG_CAP;
}
