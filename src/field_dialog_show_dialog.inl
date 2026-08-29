// field_dialog_show_dialog.inl -- Hook_show_dialog.
//
// show_dialog(window_id, state, a3) is FF8's universal text renderer.
// Called for every dialog type: regular MES, choices, tutorials, MODE_TUTO
// thoughts, battle banners, post-battle results. Regular field opcodes
// (MES/MESW/ASK/etc.) speak first via the opcode hooks; this hook fires
// AFTER the engine renders and catches anything those missed -- particularly
// MODE_TUTO thoughts and battle UI text that bypass the opcode dispatch.
//
// FFNx interaction: FFNx patches one CALL site (sub_4A0C00+0x5F) via
// replace_call, storing the original address. Our MinHook patches the
// function prologue. Call chain: FFNx wrapper -> our hook -> original.
// Both hook chains execute safely.

static char __cdecl Hook_show_dialog(int32_t window_id, uint32_t state, int16_t a3)
{
    // Call original first -- let the game render normally
    char result = s_origShowDialog(window_id, state, a3);

    // v04.19: Track ALL window_ids seen (including out-of-range)
    s_showDialogCallCount++;
    if (window_id < 0) {
        s_sdNegativeWinCount++;
    } else if (window_id < 32) {
        s_sdWinIdCounts[window_id]++;
    } else {
        s_sdWinIdCounts[31]++;  // overflow bucket
    }

    uint32_t currentMode = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xDEAD;

    // v04.19: Enhanced periodic diagnostic -- show window_id distribution
    {
        DWORD now = GetTickCount();
        if ((now - s_showDialogLastDiagTime) >= 5000 || currentMode != s_showDialogLastMode) {
            s_showDialogLastDiagTime = now;
            s_showDialogLastMode = currentMode;

            // Build window_id distribution string
            char dist[256];
            int dpos = 0;
            for (int w = 0; w < 32; w++) {
                if (s_sdWinIdCounts[w] > 0) {
                    dpos += snprintf(dist + dpos, sizeof(dist) - dpos,
                                     " w%d=%d", w, s_sdWinIdCounts[w]);
                }
            }
            if (s_sdNegativeWinCount > 0) {
                dpos += snprintf(dist + dpos, sizeof(dist) - dpos,
                                 " neg=%d", s_sdNegativeWinCount);
            }
            // v04.20: include menu_draw_text call rate
            LONG mdtCount = s_menuDrawTextCallCount;
            LONG mdtDelta = mdtCount - s_menuDrawTextLastReported;
            s_menuDrawTextLastReported = mdtCount;

            LONG gcwCount = s_gcwCallCount;
            LONG gcwDelta = gcwCount - s_gcwLastReported;
            s_gcwLastReported = gcwCount;

            LONG ufeCount = s_ufeCallCount;
            LONG ufeDelta = ufeCount - s_ufeLastReported;
            s_ufeLastReported = ufeCount;

            Log::Dialog("FieldDialog: [SHOW_DIALOG-DIAG] %d total calls, mode=%u, ufe=%ld(+%ld), mdt=%ld(+%ld), gcw=%ld(+%ld), dist:%s",
                       s_showDialogCallCount, currentMode, ufeCount, ufeDelta, mdtCount, mdtDelta, gcwCount, gcwDelta, dist);

            // v04.22: dump opcode histogram delta (only non-zero entries)
            if (s_dispatchPatched) {
                char opbuf[2048];
                int opos = 0;
                for (int op = 0; op < OPCODE_HIST_SIZE; op++) {
                    LONG cur = s_opcodeHistogram[op];
                    LONG delta = cur - s_opcodeHistogramPrev[op];
                    s_opcodeHistogramPrev[op] = cur;
                    if (delta > 0 && opos < 2000) {
                        opos += snprintf(opbuf + opos, sizeof(opbuf) - opos,
                                         " %03X=%ld", op, delta);
                    }
                }
                LONG ovfDelta = s_opcodeOverflow - s_opcodeOverflowPrev;
                s_opcodeOverflowPrev = s_opcodeOverflow;
                if (ovfDelta > 0)
                    opos += snprintf(opbuf + opos, sizeof(opbuf) - opos, " OVF=%ld", ovfDelta);
                if (opos > 0)
                    Log::Dialog("FieldDialog: [OPCODE-HIST]%s", opbuf);
            }

            // Reset counters for next interval
            memset(s_sdWinIdCounts, 0, sizeof(s_sdWinIdCounts));
            s_sdNegativeWinCount = 0;
        }
    }

    // v04.19: Log ANY call with window_id outside 0-7 range (these are
    // currently missed by our text detection and might be thoughts)
    if (window_id < 0 || window_id >= MAX_WINDOWS) {
        // Log the first 20 out-of-range calls in detail, then periodic
        static int s_oorCount = 0;
        s_oorCount++;
        if (s_oorCount <= 20) {
            Log::Dialog("FieldDialog: [SHOW_DIALOG-OOR] winId=%d state=%u a3=%d mode=%u",
                       window_id, state, (int)a3, currentMode);
        }
        return result;
    }
    if (!FF8Addresses::pWindowsArray) return result;

    uint8_t* winObj = GetWindowObj(window_id);
    if (!winObj) return result;

    char* text1 = GetWinText1(winObj);
    char* text2 = GetWinText2(winObj);  // v04.23: also check text_data2

    // v04.23: Use the first valid text pointer (prefer text1)
    char* textPtr = nullptr;
    if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00)
        textPtr = text1;
    else if (text2 && IsValidTextPointer(text2) && ProbePointer(text2) && *(const uint8_t*)text2 != 0x00)
        textPtr = text2;
    if (!textPtr) return result;

#if FLOOR_SHOT_PROBE
    FloorShotProbe((const uint8_t*)textPtr);
#endif

    // v04.23: Hash-based change detection instead of pointer-only.
    // Catches in-place buffer rewrites that the old pointer check missed.
    uint32_t hash = fnv1a_prefix((const uint8_t*)textPtr, 64);
    if (textPtr == s_sdLastTextPtr[window_id] && hash == s_sdLastHash[window_id])
        return result;  // truly unchanged
    s_sdLastTextPtr[window_id] = textPtr;
    s_sdLastHash[window_id] = hash;

    // ONE PAGE, NOT THE REST OF THE REPORT. See DecodeDialogPage in
    // field_dialog_expand.inl -- v0.70.0 put this limit here and nowhere else,
    // which fixed the reader that was already being suppressed and left the two
    // paths that were actually speaking alone.
    std::string decoded = DecodeDialogPage(textPtr, 512);  // v0.18.3.239 (#77)

    // ------------------------------------------------------------------
    // v0.109.0 (#megaflare): THE COUNTDOWN, AND THE BYTES BEHIND IT
    // ------------------------------------------------------------------
    // Aaron: "the countdown timer is displayed in a regular FF8 text box, same
    // as spell names when cast or GF names when summoned." This is that box --
    // the 2026-08-26 log has 39 [SHOW_DIALOG-TEXT] lines and ZERO [GETSTR]
    // lines, so every battle string the mod ever sees arrives right here.
    //
    // Both halves of the gate below sit above the log line, which is how ten
    // seconds of Bahamut's charge came to leave no trace at all: a one-
    // character page dies on the length half, and a page whose whole content is
    // a control code the decoder emits nothing for dies on the empty half.
    // So this runs FIRST, on the raw bytes as well as the decoded string, and
    // the [BTL-WIN-RAW] line prints what was actually in the window whether or
    // not anything could be made of it.
    if (currentMode == DLG_MODE_BATTLE) {
        uint8_t rawBuf[64];
        size_t  rawLen = 0;
        if (SafeCopyEngineText(textPtr, rawBuf, sizeof(rawBuf))) {
            while (rawLen < sizeof(rawBuf) && rawBuf[rawLen] != 0x00) rawLen++;
        }

        static unsigned s_btlRawLogged = 0;
        if (s_btlRawLogged < 400) {
            s_btlRawLogged++;
            Log::Dialog("FieldDialog: [BTL-WIN-RAW] win[%d] len=%u decoded=\"%s\" bytes=%s",
                        window_id, (unsigned)rawLen, decoded.c_str(),
                        FF8TextDecode::HexDump(rawBuf, rawLen).c_str());
        }

        const int cd = BwcCountdownValue(decoded.c_str(), rawBuf, rawLen);
        static int   s_bwcLastValue = BWC_NONE;
        static DWORD s_bwcLastTick  = 0;
        DWORD nowTick = GetTickCount();
        if (BwcShouldAnnounce(cd, s_bwcLastValue,
                              (unsigned)(nowTick - s_bwcLastTick))) {
            s_bwcLastValue = cd;
            s_bwcLastTick  = nowTick;

            char msg[64];
            BwcAnnounceText(msg, sizeof(msg), cd);
            Log::Dialog("FieldDialog: [COUNTDOWN-BOX] win[%d] value=%d -- \"%s\"",
                        window_id, cd, msg);

            // Aaron: "This also needs to be assertive and interrupt any other
            // text so it can't be missed."
            ScreenReader::Speak(msg, true);

            // Claim the text so the ordinary reader below does not follow the
            // alert with a bare "5" a moment later.
            s_sdLastDecoded[window_id] = decoded;
            return result;
        }
        if (cd != BWC_NONE) {
            // Recognised, but too soon to say again -- still claim it.
            s_sdLastDecoded[window_id] = decoded;
            return result;
        }
    }

    // v0.107.0 (#megaflare): THE GATE, AND -- FOR THE FIRST TIME -- A RECORD OF
    // WHAT IT THROWS AWAY. This return used to be silent and it sat above the
    // [SHOW_DIALOG-TEXT] line, so a one- or two-character window text left no
    // trace anywhere: not spoken, not logged, not countable. See
    // dialog_short_text_model.inl for why battle digits now come through and
    // why nothing else does.
    if (!DlgTextPassesLengthGate(decoded.c_str(), decoded.length(),
                                 currentMode, MIN_TEXT_LENGTH)) {
        static unsigned s_shortDropLogged = 0;
        if (DlgShortDropWorthLogging(decoded.c_str(), decoded.length(),
                                     currentMode, MIN_TEXT_LENGTH) &&
            DlgShortLogAllowed(s_shortDropLogged)) {
            s_shortDropLogged++;
            Log::Dialog("FieldDialog: [SHOW_DIALOG-SHORT] win[%d] mode=%u len=%u dropped=\"%s\"",
                        window_id, currentMode, (unsigned)decoded.length(),
                        decoded.c_str());
        }
        return result;
    }

    // Dedup against last decoded for this window in show_dialog
    if (decoded == s_sdLastDecoded[window_id]) return result;
    s_sdLastDecoded[window_id] = decoded;

    // v04.23: Log text changes with transition info for debugging
    uint8_t tutoId = FF8Addresses::pCurrentTutorialId ?
                     *FF8Addresses::pCurrentTutorialId : 0xFF;
    int16_t transition = GetWinOpenCloseTransition(winObj);
    bool usedText2 = (textPtr == text2 && textPtr != text1);
    Log::Dialog("FieldDialog: [SHOW_DIALOG-TEXT] win[%d] mode=%u tutoId=%u state=%u tr=%d%s text=\"%s\"",
               window_id, currentMode, (unsigned)tutoId, state, (int)transition,
               usedText2 ? " [T2]" : "", decoded.c_str());

    // v0.15.1: forward EVERY decoded field-dialog text to chase_ask_overlay.
    // Cheap strncmp filter inside; opens the chase ASK when it sees Squall's
    // "Forget it!  Let's go!" trigger MES in a chase field. Outside of chase
    // context this is a near-no-op (single string compare + early return).
    // v0.107.0 (#megaflare): forward battle-window text to BattleTTS, which
    // owns the screenshot burst it may arm. The string it looks for lives next
    // to the capture code rather than here.
    if (currentMode == DLG_MODE_BATTLE) {
        ::BattleTTS::NoteBattleWindowText(decoded.c_str());
    }

    if (currentMode == 1 /* MODE_FIELD */) {
        ::ChaseAskOverlay::OnDialogText(decoded.c_str());
        // v0.18.3.23: same forward for the Timber train guard-mode ASK (#60).
        // Cheap field-gate + strstr inside; near-no-op outside tiyane1.
        ::TrainModeAskOverlay::OnDialogText(decoded.c_str());
        // v0.20.104: and the Garden-battle mini-game legend (#minigame-bgbtl).
        ::FieldNavigation::GardenBattleOnDialogText(decoded.c_str());
    }

    // Check if opcode hooks already spoke this
    EnterCriticalSection(&s_cs);
    WindowState& ws = s_winState[window_id];
    bool alreadySpoken = (decoded == ws.lastSpokenText || decoded == ws.lastRawText);

    if (!alreadySpoken) {
        for (int w = 0; w < MAX_WINDOWS && !alreadySpoken; w++) {
            if (w == window_id) continue;
            if (decoded == s_winState[w].lastSpokenText ||
                decoded == s_winState[w].lastRawText)
                alreadySpoken = true;
        }
    }

    // v04.18: Also check suffix/substring (opcode hooks might have spoken
    // a longer version that includes this text)
    if (!alreadySpoken) {
        for (int w = 0; w < MAX_WINDOWS; w++) {
            if (IsSuffixOrSubstring(s_winState[w].lastSpokenText, decoded) ||
                IsSuffixOrSubstring(s_winState[w].lastRawText, decoded)) {
                alreadySpoken = true;
                break;
            }
        }
    }

    // v0.69.0: A SCROLLING WINDOW, which neither test above can see. See
    // ContinuationTail for the Ragnarok terminal that proved it. `decoded` is
    // replaced by whatever part of it has not been read out yet, so a long
    // passage is spoken once, in order, across however many scrolls the engine
    // takes to show it.
    std::string scrollWhole;   // set when a scroll was detected; see below
    if (!alreadySpoken && !ws.lastSpokenText.empty()) {
        const std::string tail = ContinuationTail(ws.lastSpokenText, decoded);
        if (tail.empty()) {
            alreadySpoken = true;
            Log::Dialog("FieldDialog: [SHOW_DIALOG-SCROLL] win[%d] adds nothing new "
                        "-- the window scrolled, the words are already said", window_id);
        } else if (tail.length() != decoded.length()) {
            Log::Dialog("FieldDialog: [SHOW_DIALOG-SCROLL] win[%d] scrolled: speaking "
                        "the %u new characters, not the %u it re-rendered",
                        window_id, (unsigned)tail.length(), (unsigned)decoded.length());
            // The window's record has to keep EVERYTHING said so far, not the
            // fragment: the next scroll is measured against the whole passage.
            scrollWhole = ws.lastSpokenText + tail;
            decoded = tail;
        }
    }

    if (!alreadySpoken) {
        s_lastTutoText = decoded;
        s_lastTutoSpeakTime = GetTickCount();
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
        if (!scrollWhole.empty()) {
            // Remember the passage, not the fragment of it we just read out.
            ws.lastSpokenText = scrollWhole;
            ws.lastRawText    = scrollWhole;
        }
        ws.skipLogged = false;

        MarkPendingAsSpoken(decoded);

        // v0.10.112: In battle mode, prepend character name to "Received" draw results.
        // "Received 4 Blizzards!" -> "Squall received 4 Blizzards!"
        std::string speakText = decoded;
        if (currentMode == 3 && decoded.length() > 8 && decoded.compare(0, 8, "Received") == 0) {
            BattleTTS::ValidateDrawCharacter(0);  // diff inventories, store result
            const char* drawer = BattleTTS::GetLastDrawerName();
            if (drawer) {
                speakText = std::string(drawer) + " r" + decoded.substr(1);
            }
        }

        // v0.14.63: In battle mode, suppress only when the Scan UI window is
        // currently open. ScanTTS::IsScreenActive() returns true between
        // OnScanPopupSpawn (first sub_B687C0 fire -- the moment the scan window
        // renders on screen) and OnScanPopupDespawn (when the player dismisses
        // it). During that window, the rendered scan text (name, description,
        // "LEVEL X HP cur/max") would duplicate the scan_tts.cpp auto-announce
        // -- ScanTTS owns the announce, this hook stays silent. Outside the
        // scan window (and outside battle entirely), all battle UI text speaks
        // normally: "Cast Fire" spell-cast banners, mid-battle cutscene dialog,
        // and any other window text the engine renders via show_dialog.
        //
        // Sequencing note: HookedScanGetText sets s_scanScreenActiveSlot BEFORE
        // returning the text to the engine, and our show_dialog hook reads the
        // window text AFTER calling the original. So by the time we check
        // IsScreenActive() here, the flag is already set on the first scan
        // render. v0.14.62's blanket battle-mode suppression silenced too much;
        // v0.14.63 narrows the gate to only the scan-active period.
        bool suppressForScan = (currentMode == 3 && ScanTTS::IsScreenActive());
        if (suppressForScan) {
            Log::Dialog("FieldDialog: [SHOW_DIALOG-SUPPRESS] win[%d] mode=3 scan-active text=\"%s\" "
                       "-- ScanTTS owns the announce while scan window is open",
                       window_id, decoded.c_str());
        } else {
            Log::Dialog("FieldDialog: [SHOW_DIALOG-SPEAK] win[%d] mode=%u Speaking: \"%s\"",
                       window_id, currentMode, speakText.c_str());
            s_lastDialogSpoken = speakText;  // v04.25: track for F5 repeat
            ScreenReader::Speak(speakText.c_str(), false);  // Queue mode
        }
    } else {
        Log::Dialog("FieldDialog: [SHOW_DIALOG-TEXT] win[%d] (already spoken by opcode hook)",
                   window_id);
    }
    LeaveCriticalSection(&s_cs);

    return result;
}
