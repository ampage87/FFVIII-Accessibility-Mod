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

    std::string decoded = DecodeDialogWithExpansion(textPtr, 512);  // v0.18.3.239 (#77)
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return result;

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

    if (!alreadySpoken) {
        s_lastTutoText = decoded;
        s_lastTutoSpeakTime = GetTickCount();
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
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
