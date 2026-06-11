// field_dialog_scan.inl -- the central TTS-speak path.
//
// ScanAndSpeakAllWindows iterates the 8-window slot array, decodes text, and
// speaks anything that hasn't been spoken yet (with per-window + cross-window
// dedup against lastSpokenText and lastRawText).
//
// ScanAndSpeakChoiceWindows handles ASK/AASK choice dialogs with their own
// dedup against the prompt + selected-choice index. Falls through to
// ScanAndSpeakAllWindows at the end to catch any non-choice text in adjacent
// slots.
//
// MarkPendingAsSpoken is the bridge between the field_get_dialog_string hook
// (which queues text fetched out-of-band) and the speak path. Whenever any
// scan speaks something, it marks matching pending entries so the deferred
// poll-thread speak path doesn't re-announce them.
//
// CheckPendingTexts runs from PollWindows. It speaks any pending text that
// hasn't been picked up by an opcode hook within PENDING_SPEAK_DELAY_MS,
// catching off-screen dialog (Squall's thoughts) that bypass the windows.
//
// v0.17.8.1: IsGarbledText() filter rejects decoded buffers that look like
// stale / uninitialized memory rather than real FF8 dialog. See the function
// header for the heuristic rules and the bug context.

// ============================================================================
// v0.17.8.1: Garbled-text rejection filter (Fire Cavern bug #3)
// ============================================================================
//
// Aaron's 2026-05-18 Fire Cavern playthrough logged this after a tutorial
// scene completed ([TUTO] mode 10 -> 1):
//
//   [POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."
//
// Decomposed against ff8_text_decode.cpp: every byte in the buffer decoded
// successfully -- digits (0x21-0x2A), letters (0x45-0x78), punctuation
// (0x2B-0x44), and the two-char compression sequences (0xE8-0xFF: "in",
// "re", "to", "ne", "HP", "l "). The decoder did nothing wrong. The bytes
// themselves were stale tutorial-overlay buffer data that the engine left
// in win[0]'s text region after the tutorial torn down, and the poll loop
// then decoded them as if they were real dialog.
//
// The signature of this kind of stale-buffer garbage versus real FF8 dialog:
// 1. Letter-digit boundaries with no space separator. Real FF8 dialog
//    keeps digits in standalone tokens ("5 Potions", "Level 3"). Stale
//    buffers produce sequences like "3in", "retone3", "08E" -- a digit
//    adjacent to a letter inside what would otherwise be a word.
// 2. Unusual punctuation density. The character table has rare-in-dialog
//    glyphs at 0x2B (%), 0x34 (*), 0x41 (#), 0x42 (USD), 0x33 (=),
//    0x35 (&), 0x44 (_), 0x2C (/), 0x31 (+). Real dialog uses them
//    sparingly; stale buffers hit them at random byte values, pushing
//    density above ~10%.
// 3. Low letter ratio. Real dialog is mostly letters and spaces.
//    Stale buffers have letters scattered among digits and punctuation.
// 4. "[NameXX]" literals. DecodeByte emits this when a 0x03 name-
//    substitution byte has a name ID outside 0x30-0x3D (Squall through
//    Boko). Real text never has these; stale buffers do.
//
// The filter combines all four signals with thresholds tuned against the
// canonical garbage sample. We reject only when length >= 8 -- short
// fragments like "Yes." or "OK" don't have enough data to judge and the
// short-text MIN_TEXT_LENGTH gate above already catches the most extreme
// cases.
//
// Trade-off: a deliberately weird-looking line of legit dialog (heavy on
// punctuation, mixed with stat numbers) could in principle be rejected.
// We mitigate by requiring TWO independent signals to fire, not one. If
// the BAT shows legit dialog being suppressed, lower the digit-transition
// or punctuation thresholds.
//
// Punctuation byte values flagged as "unusual" (hex literals used to avoid
// edit-tool issues with the dollar-sign source character):
//   0x2A '*'   0x25 '%'   0x23 '#'   0x24 dollar   0x2B '+'
//   0x3D '='   0x26 '&'   0x5F '_'   0x2F '/'
static bool IsGarbledText(const std::string& text)
{
    const int len = (int)text.length();
    if (len < 8) return false;  // too short to judge reliably

    // Signal 4 (immediate disqualifier): [NameXX] literal substring
    if (text.find("[Name") != std::string::npos) return true;

    int letterCount = 0;
    int unusualPunctCount = 0;
    int letterDigitTransitions = 0;
    int lowerToUpperTransitions = 0;  // v0.17.8.1.1: random mid-word caps

    unsigned char prev = 0;
    for (int i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)text[i];

        const bool curLower = (c >= 'a' && c <= 'z');
        const bool curUpper = (c >= 'A' && c <= 'Z');
        const bool curAlpha = curLower || curUpper;
        const bool curDigit = (c >= '0' && c <= '9');

        if (curAlpha) {
            letterCount++;
        } else if (!curDigit) {
            // Unusual punctuation classification by ASCII value
            // (avoids dollar-sign literal in source for edit-tool safety).
            if (c == 0x2A || c == 0x25 || c == 0x23 || c == 0x24 ||
                c == 0x2B || c == 0x3D || c == 0x26 || c == 0x5F ||
                c == 0x2F) {
                unusualPunctCount++;
            }
            // Common punctuation (. , ! ? ' " : ; - ( ) ~ space) is not
            // counted as unusual; real dialog uses these freely.
        }

        if (i > 0) {
            const bool prevLower = (prev >= 'a' && prev <= 'z');
            const bool prevUpper = (prev >= 'A' && prev <= 'Z');
            const bool prevAlpha = prevLower || prevUpper;
            const bool prevDigit = (prev >= '0' && prev <= '9');
            if ((prevAlpha && curDigit) || (prevDigit && curAlpha)) {
                letterDigitTransitions++;
            }
            // v0.17.8.1.1: lowercase immediately followed by uppercase is
            // a hallmark of stale-buffer garbage ("wlNVFEC", "RJtVPNR",
            // "FNdV", "aVme"). Real FF8 dialog capitalizes only at word
            // starts (after a space) or in all-caps words ("SeeD",
            // "SAY WHAT"), so a legit line has at most 1-2 of these
            // (e.g. the single e->D in "SeeD"). Garbage has many.
            if (prevLower && curUpper) {
                lowerToUpperTransitions++;
            }
        }
        prev = c;
    }

    const int letterPct       = (letterCount * 100) / len;
    const int unusualPunctPct = (unusualPunctCount * 100) / len;

    // v0.17.8.1.1: Strong standalone signals -- any ONE is decisive. These
    // thresholds are set high enough that well-formed English dialog of any
    // length stays comfortably below them. The v0.17.8.1 BAT failure was a
    // ~400-char tutorial-overlay buffer whose long letter-heavy tail diluted
    // the punctuation-density and letter-ratio signals below the old
    // thresholds, leaving only the letter-digit-transition signal -- which
    // the old "require 2 signals" rule then ignored. The two transition
    // counters below are the reliable discriminators: real dialog has
    // near-zero of either.
    if (lowerToUpperTransitions >= 5) return true;  // random capitalization
    if (letterDigitTransitions  >= 4) return true;  // digits glued to letters
    if (unusualPunctPct > 15)         return true;  // punctuation soup
    if (letterPct < 30)               return true;  // almost no letters

    // Weaker signals -- require any TWO to fire. Catches the shorter
    // canonical sample (",e 3in*retone3 e~HP~B:All08E%~!/...") which trips
    // the digit-transition and punctuation-density signals together.
    int signalCount = 0;
    if (letterDigitTransitions  >= 2) signalCount++;
    if (lowerToUpperTransitions >= 3) signalCount++;
    if (unusualPunctPct > 8)          signalCount++;
    if (letterPct < 45)               signalCount++;

    return (signalCount >= 2);
}

// ============================================================================
// Core: scan ALL windows and speak any new text
//
// Called after every opcode handler returns (and at end of choice handler).
// Checks all 8 windows for text that hasn't been spoken yet. Deduplicates
// against both lastSpokenText and lastRawText so choice windows (whose
// formatted text differs from raw decoded text) are naturally skipped.
// ============================================================================

static void ScanAndSpeakAllWindows(const char* opcodeLabel)
{
    if (!FF8Addresses::pWindowsArray) return;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        WindowState& ws = s_winState[i];

        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);

        // Skip windows with no valid text
        if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1))
            continue;
        if (*(const uint8_t*)text1 == 0x00) {
            // Window was cleared -- reset choice state if it was active
            ws.Reset();
            continue;
        }

        std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
        if (decoded.empty()) continue;

        // v04.16: Skip very short text fragments (stale data, control codes)
        if ((int)decoded.length() < MIN_TEXT_LENGTH) continue;

        // Exact duplicate for this window -- skip
        if (decoded == ws.lastSpokenText || decoded == ws.lastRawText)
            continue;

        // Page advance: new text is a portion of what this window already spoke
        if (IsSuffixOrSubstring(ws.lastSpokenText, decoded) ||
            IsSuffixOrSubstring(ws.lastRawText, decoded)) {
            if (!ws.skipLogged) {
                ws.skipLogged = true;
                Log::Dialog("FieldDialog: [%s] win[%d] Skipping page advance (already spoken)",
                           opcodeLabel, i);
            }
            continue;
        }

        // Also check if this text was just spoken in ANY other window
        bool spokenElsewhere = false;
        for (int j = 0; j < MAX_WINDOWS; j++) {
            if (j == i) continue;
            if (decoded == s_winState[j].lastSpokenText ||
                decoded == s_winState[j].lastRawText) {
                spokenElsewhere = true;
                break;
            }
        }
        if (spokenElsewhere) continue;

        // v0.17.8.1: Garbled-text filter. Stale tutorial-overlay buffer
        // data was producing strings like ",e 3in*retone3 e~HP~B:All08E%~!/..."
        // after [TUTO] mode 10->1 (Fire Cavern bug #3, Aaron 2026-05-18).
        // IsGarbledText uses 4 heuristic signals (letter/digit transitions,
        // unusual-punctuation density, letter ratio, [NameXX] markers)
        // and requires TWO to fire. We still mark the buffer as "spoken"
        // (updating ws.lastSpokenText + the pending queue) so subsequent
        // poll ticks don't re-detect and re-log the same garbage. Only
        // the ScreenReader::Speak call is suppressed.
        if (IsGarbledText(decoded)) {
            ws.lastSpokenText = decoded;
            ws.lastRawText = decoded;
            ws.skipLogged = false;
            MarkPendingAsSpoken(decoded);
            Log::Dialog("FieldDialog: [%s] win[%d] REJECTED garbled: \"%s\"",
                       opcodeLabel, i, decoded.c_str());
            continue;
        }

        // New text for this window -- speak it. Dedup keys stay the ORIGINAL
        // decoded text so the next poll's raw window text still matches them
        // and we don't re-speak; only the spoken/repeat copy is rewritten.
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
        ws.skipLogged = false;

        // v04.16: Mark this text as spoken in pending queue
        MarkPendingAsSpoken(decoded);

        // v0.18.3.28 (#60/#57): rewrite the Timber code-entry instruction so the
        // four "L L L L" button sprites become real key names (A, D, X, W for
        // the example code 3124) plus a "/" reminder. No-op on every other line.
        std::string toSpeak = decoded;
        ApplyTrainCodeKeyFix(toSpeak);

        Log::Dialog("FieldDialog: [%s] win[%d] Speaking: \"%s\"",
                   opcodeLabel, i, toSpeak.c_str());
        s_lastDialogSpoken = toSpeak;  // v04.25: track for F5 repeat
        ScreenReader::Speak(toSpeak.c_str(), false);  // Queue mode
    }
}

// ============================================================================
// Core: scan ALL windows for choice dialogs
//
// Choice opcodes (ASK/AASK) need special handling for choice navigation.
// We scan all windows for any that have valid firstQ/lastQ fields.
// The choice handler stores lastRawText so that the all-windows scanner
// (called at the end) naturally deduplicates against choice windows.
// ============================================================================

static void ScanAndSpeakChoiceWindows(const char* opcodeLabel)
{
    if (!FF8Addresses::pWindowsArray) return;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);

        if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1))
            continue;
        if (*(const uint8_t*)text1 == 0x00)
            continue;

        uint8_t firstQ = *(uint8_t*)(winObj + WIN_OBJ_FIRST_Q_OFFSET);
        uint8_t lastQ = *(uint8_t*)(winObj + WIN_OBJ_LAST_Q_OFFSET);
        uint8_t curChoice = *(uint8_t*)(winObj + WIN_OBJ_CUR_CHOICE_OFFSET);

        // Only process this window as a choice dialog if it has valid choice fields
        if (firstQ == 0 && lastQ == 0) continue;
        if (lastQ < firstQ) continue;
        // v0.15.9.9.1: FF8's "no ASK fields set" sentinel is 0xFF (255).
        // When a previous MES leaves text in a window slot but the slot
        // never had ASK fields set (e.g. slot 0 holding Squall's chase-
        // trigger "Let's go!" line while chase_ask_overlay opens its ASK
        // in slot 2), this hook iterates over slot 0 and treats it as a
        // choice dialog -- speaking the stale prompt as a duplicate. The
        // (0, 0) check above only catches the all-zeros sentinel; the
        // (lastQ < firstQ) check only catches inverted ranges; (0xFF,
        // 0xFF) slipped through both. v0.15.9.9 BAT confirmed this is
        // the source of the duplicate "Let's go!" that the ASK prompt
        // change couldn't eliminate (since the prompt change only
        // affected slot 2, while the duplicate was coming from slot 0).
        // 0xFF is FF8's universal "unset" sentinel for these uint8_t
        // fields and cannot represent a legitimate ASK (FF8 dialogs cap
        // at ~16 choices, so neither firstQ nor lastQ is ever 0xFF in
        // a real ASK).
        if (firstQ == 0xFF || lastQ == 0xFF) continue;

        FF8TextDecode::ChoiceDialog dialog =
            FF8TextDecode::DecodeChoices((const uint8_t*)text1, 512, firstQ, lastQ);

        if (dialog.prompt.empty() && dialog.choices.empty()) continue;

        WindowState& ws = s_winState[i];

        int choiceIndex = (int)curChoice - (int)firstQ;
        int numChoices = (int)(lastQ - firstQ + 1);

        // Same dialog, same choice = skip (do nothing)
        if (dialog.prompt == ws.lastChoicePrompt && curChoice == ws.lastSpokenChoice)
            continue;

        // Same dialog, different choice = interrupt with new choice text
        if (dialog.prompt == ws.lastChoicePrompt && curChoice != ws.lastSpokenChoice) {
            ws.lastSpokenChoice = curChoice;

            if (choiceIndex >= 0 && choiceIndex < (int)dialog.choices.size()) {
                Log::Dialog("FieldDialog: [%s] win[%d] Choice changed -> %d: \"%s\"",
                           opcodeLabel, i, choiceIndex + 1,
                           dialog.choices[choiceIndex].c_str());
                ScreenReader::Speak(dialog.choices[choiceIndex].c_str(), true);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "Choice %d of %d", choiceIndex + 1, numChoices);
                ScreenReader::Speak(buf, true);
            }
            continue;
        }

        // New choice dialog
        ws.lastChoicePrompt = dialog.prompt;
        ws.lastSpokenChoice = curChoice;
        ws.skipLogged = false;

        Log::Dialog("FieldDialog: [%s] win[%d] Parsed %d choices (firstQ=%u lastQ=%u curChoice=%u)",
                   opcodeLabel, i, (int)dialog.choices.size(), firstQ, lastQ, curChoice);

        std::string fullText = dialog.prompt;
        if (!dialog.choices.empty()) {
            for (int c = 0; c < (int)dialog.choices.size(); c++) {
                fullText += ". ";
                if (c == choiceIndex)
                    fullText += "Selected: ";
                fullText += dialog.choices[c];
            }
        }

        ws.lastSpokenText = fullText;
        // Also store raw text so the all-windows scanner won't re-speak it
        ws.lastRawText = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));

        // v04.16: Mark both versions as spoken in pending queue
        MarkPendingAsSpoken(fullText);
        MarkPendingAsSpoken(ws.lastRawText);

        Log::Dialog("FieldDialog: [%s] win[%d] Speaking: \"%s\"",
                   opcodeLabel, i, fullText.c_str());
        s_lastDialogSpoken = fullText;  // v04.25: track for F5 repeat
        ScreenReader::Speak(fullText.c_str(), false);
    }

    // Also scan all windows for any new regular dialog
    // (another window might have new text alongside the choice window).
    // The all-windows scanner will naturally skip choice windows because
    // lastRawText matches the current decoded text.
    ScanAndSpeakAllWindows(opcodeLabel);
}

// ============================================================================
// v04.16: Mark pending text as spoken (called when opcode hooks speak text)
// ============================================================================

static void MarkPendingAsSpoken(const std::string& spokenText)
{
    for (int i = 0; i < s_pendingCount; i++) {
        if (!s_pending[i].spoken && s_pending[i].decoded == spokenText) {
            s_pending[i].spoken = true;
        }
    }
}

// ============================================================================
// v04.16: Check pending texts and speak any that haven't been spoken
// by opcode hooks within the timeout. Called from PollWindows.
// ============================================================================

static void CheckPendingTexts()
{
    DWORD now = GetTickCount();

    for (int i = 0; i < s_pendingCount; i++) {
        if (s_pending[i].spoken) continue;
        if ((now - s_pending[i].fetchTime) < PENDING_SPEAK_DELAY_MS) continue;

        // This text was fetched 500ms+ ago and never spoken by opcode hooks.
        // Check if it matches any window's lastSpokenText (opcode hook might
        // have spoken it without exact string match due to formatting).
        bool alreadySpoken = false;
        for (int w = 0; w < MAX_WINDOWS; w++) {
            if (s_pending[i].decoded == s_winState[w].lastSpokenText ||
                s_pending[i].decoded == s_winState[w].lastRawText ||
                IsSuffixOrSubstring(s_winState[w].lastSpokenText, s_pending[i].decoded) ||
                IsSuffixOrSubstring(s_winState[w].lastRawText, s_pending[i].decoded)) {
                alreadySpoken = true;
                break;
            }
        }

        s_pending[i].spoken = true;  // Mark as handled either way

        if (!alreadySpoken) {
            // v0.17.8.1: Garbled-text filter (same as ScanAndSpeakAllWindows).
            // The deferred path can also receive stale-buffer garbage via
            // field_get_dialog_string fetches that happened during a
            // tutorial overlay teardown.
            if (IsGarbledText(s_pending[i].decoded)) {
                Log::Dialog("FieldDialog: [GETSTR-DEFERRED] msgId=%d REJECTED garbled: \"%s\"",
                           s_pending[i].messageId, s_pending[i].decoded.c_str());
            } else {
                Log::Dialog("FieldDialog: [GETSTR-DEFERRED] msgId=%d Speaking: \"%s\"",
                           s_pending[i].messageId, s_pending[i].decoded.c_str());
                ScreenReader::Speak(s_pending[i].decoded.c_str(), false);
            }
        }
    }

    // Compact: remove old spoken entries
    int writeIdx = 0;
    for (int i = 0; i < s_pendingCount; i++) {
        if (!s_pending[i].spoken || (now - s_pending[i].fetchTime) < 2000) {
            if (writeIdx != i) s_pending[writeIdx] = s_pending[i];
            writeIdx++;
        }
    }
    s_pendingCount = writeIdx;
}
