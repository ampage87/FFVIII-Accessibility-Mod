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

        // New text for this window -- speak it
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
        ws.skipLogged = false;

        // v04.16: Mark this text as spoken in pending queue
        MarkPendingAsSpoken(decoded);

        Log::Dialog("FieldDialog: [%s] win[%d] Speaking: \"%s\"",
                   opcodeLabel, i, decoded.c_str());
        s_lastDialogSpoken = decoded;  // v04.25: track for F5 repeat
        ScreenReader::Speak(decoded.c_str(), false);  // Queue mode
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
            Log::Dialog("FieldDialog: [GETSTR-DEFERRED] msgId=%d Speaking: \"%s\"",
                       s_pending[i].messageId, s_pending[i].decoded.c_str());
            ScreenReader::Speak(s_pending[i].decoded.c_str(), false);
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
