// field_dialog_opcodes.inl -- the dialog opcode hooks.
//
// Each Hook_opcode_XXX wraps an FF8 JSM-interpreter opcode handler. We let
// the original run first (it sets up window state and text pointers), then
// scan the windows for new text via the scan.inl helpers. ASK/AASK route
// through ScanAndSpeakChoiceWindows so they get choice-cursor tracking; the
// rest route through ScanAndSpeakAllWindows.
//
// Hook_field_get_dialog_string sits one layer below the opcode hooks at
// FF8's "fetch message text by ID" function. It's the catch-all for text
// fetched out-of-band (Squall's thoughts, dialog from non-script paths).
// It queues into s_pending; CheckPendingTexts in scan.inl drains the queue
// from the poll thread if no opcode hook claimed the text in time.
//
// RepeatLastDialog is the F5-hotkey handler that replays s_lastDialogSpoken.

// ============================================================================
// v04.16: field_get_dialog_string hook
//
// This is the low-level function that fetches message text from the field's
// message table. Called by ALL dialog opcodes, but ALSO by non-standard
// code paths (Squall's thoughts, etc.) that bypass the window system.
//
// We log every call and store the text as "pending". If it gets spoken by
// an opcode hook within 500ms, we mark it spoken. Otherwise, PollWindows
// speaks it as a thought/off-screen dialog.
// ============================================================================

// v04.23: Fixed signature to match FFNx: (char* msgBase, int dialogId)
static char* __cdecl Hook_field_get_dialog_string(char* msgBase, int dialogId)
{
    // v0.15.6 Phase 2b: override path. When DialogInject has activated the
    // override flag (which it does just before calling opcode_ask), short-
    // circuit and return our FF8-encoded buffer instead of the natural game
    // data. set_window_object_ASK consumes the returned char* and stores it
    // in slot+0x08, where the engine reads it every frame for the dialog's
    // lifetime. DialogInject's buffer is statically allocated and lives long
    // enough.
    //
    // We log the override path so BAT logs clearly show when the substitution
    // happened. Error guards: if the override flag is set but the pointer is
    // null (shouldn't happen given DialogInject's discipline of setting both
    // together), fall through to the original to avoid crashes.
    if (::DialogInject::IsOverrideActive()) {
        const char* overrideText = ::DialogInject::GetOverrideText();
        if (overrideText != nullptr) {
            Log::Dialog("FieldDialog: [GETSTR-OVERRIDE] DialogInject providing custom text "
                        "(orig msgBase=0x%08X dialogId=%d -> override=0x%08X)",
                        (uint32_t)(uintptr_t)msgBase, dialogId,
                        (uint32_t)(uintptr_t)overrideText);
            return (char*)overrideText;
        }
        Log::Dialog("FieldDialog: [GETSTR-OVERRIDE] flag set but text is null; falling through");
    }

    char* result = s_origGetDialogString(msgBase, dialogId);

    // Diagnostic: log first few calls unconditionally, then periodic summary
    s_getstrCallCount++;
    if (s_getstrCallCount <= 10) {
        Log::Dialog("FieldDialog: [GETSTR-RAW] call#%d base=0x%08X dialogId=%d result=0x%08X",
                   s_getstrCallCount, (uint32_t)(uintptr_t)msgBase, dialogId,
                   (uint32_t)(uintptr_t)result);
    } else {
        DWORD now = GetTickCount();
        if ((now - s_getstrLastDiagTime) >= 5000) {
            s_getstrLastDiagTime = now;
            Log::Dialog("FieldDialog: [GETSTR-DIAG] %d total calls so far", s_getstrCallCount);
        }
    }

    if (!ProbeGetstrResult(result)) return result;

    // v0.18.3.241 (#77) DIAGNOSTIC — GETSTR-HEX.
    //
    // The .240 BAT ruled out both engine expanders AND our own 0x0A resolver:
    // neither [TEXTEXPAND] (hooks never fired) nor [VAR-EXPAND] (no 0x0A byte
    // found in the raw message) appeared, yet the Tomb ID still spoke as
    // "Student ID No. .". So the number is NOT carried by control code 0x0A in
    // this message — the two-guesses-in-a-row rule says stop guessing and read
    // the bytes.
    //
    // This dumps the raw FF8-encoded message so the next BAT tells us exactly
    // which control code sits between "No." and the period. Gated to messages
    // that decode with a suspicious gap so it can't flood the log; retire
    // (set GETSTR_HEX_DIAG 0) once #77 is closed.
    // v0.18.3.245 (#78): back OFF. The .244 attempt put the hex dump here in the
    // GETSTR hook, but Xu's briefing flows through the AMESW window-scan path,
    // so it never fired. The byte capture now lives in DecodeDialogWithExpansion
    // (VAR_EXPAND_HEX_DIAG), which is on the scan path. Kept behind the gate.
    #define GETSTR_HEX_DIAG 0
    #if GETSTR_HEX_DIAG
    {
        static int s_hexDumps = 0;
        if (s_hexDumps < 12) {
            uint8_t hexBuf[96];
            if (SafeCopyEngineText(result, hexBuf, sizeof(hexBuf))) {
                size_t n = 0;
                while (n < sizeof(hexBuf) && hexBuf[n] != 0x00) n++;
                std::string hex = FF8TextDecode::HexDump(hexBuf, n);
                std::string plain = TrimDecoded(FF8TextDecode::Decode(hexBuf, n));
                s_hexDumps++;
                Log::Dialog("FieldDialog: [GETSTR-HEX] dialogId=%d len=%u text=\"%s\" bytes=%s",
                            dialogId, (unsigned)n, plain.c_str(), hex.c_str());
            }
        }
    }
    #endif

    // v0.18.3.239 (#77): substitutes the engine's expanded text when the
    // message carries a {Var} numeric insert (e.g. the Tomb student ID).
    std::string decoded = DecodeDialogWithExpansion(result, 512);
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return result;

    // Skip if identical to last fetch (opcodes call this multiple times)
    if (decoded == s_lastGetstrText) return result;
    s_lastGetstrText = decoded;

    Log::Dialog("FieldDialog: [GETSTR] dialogId=%d text=\"%s\"", dialogId, decoded.c_str());

    // Store as pending
    EnterCriticalSection(&s_cs);

    // Check if already in pending list
    bool alreadyPending = false;
    for (int i = 0; i < s_pendingCount; i++) {
        if (s_pending[i].decoded == decoded) {
            alreadyPending = true;
            break;
        }
    }

    if (!alreadyPending) {
        // Shift out oldest if full
        if (s_pendingCount >= MAX_PENDING) {
            for (int i = 1; i < MAX_PENDING; i++)
                s_pending[i - 1] = s_pending[i];
            s_pendingCount = MAX_PENDING - 1;
        }
        s_pending[s_pendingCount].decoded = decoded;
        s_pending[s_pendingCount].fetchTime = GetTickCount();
        s_pending[s_pendingCount].spoken = false;
        s_pending[s_pendingCount].messageId = dialogId;
        // v0.18.3.250 (#80): snapshot the raw FF8 bytes so CheckPendingTexts
        // can re-decode at drain time with insert values the engine populates
        // after this fetch. SafeCopyEngineText is SEH-guarded; on any failure
        // force raw[0]=0 so the drain falls back to the fetch-time decode
        // (a partial copy after a fault could otherwise be unterminated).
        // v0.18.3.251 (#80): also store the LIVE pointer -- the .250 BAT proved
        // the engine mutates the message buffer after this fetch, so the drain
        // must re-read the live bytes; the snapshot is now only the fallback.
        s_pending[s_pendingCount].rawPtr = result;
        if (!SafeCopyEngineText(result, s_pending[s_pendingCount].raw,
                                sizeof(s_pending[s_pendingCount].raw))) {
            s_pending[s_pendingCount].raw[0] = 0x00;
        }
        s_pendingCount++;
    }

    LeaveCriticalSection(&s_cs);
    return result;
}

// ============================================================================
// v04.18: opcode_tuto hook -- catches when the game triggers tutorial/thought
// This opcode sets game mode to MODE_TUTO. By hooking it we can see exactly
// when thoughts/tutorials are triggered and what tutorial ID is used.
// ============================================================================

static int __cdecl Hook_opcode_tuto(int entityPtr)
{
    uint8_t tutoIdBefore = FF8Addresses::pCurrentTutorialId ?
                           *FF8Addresses::pCurrentTutorialId : 0xFF;
    uint16_t modeBefore = FF8Addresses::pGameMode ?
                          *FF8Addresses::pGameMode : 0xFFFF;

    int result = s_origTuto(entityPtr);

    uint8_t tutoIdAfter = FF8Addresses::pCurrentTutorialId ?
                          *FF8Addresses::pCurrentTutorialId : 0xFF;
    uint16_t modeAfter = FF8Addresses::pGameMode ?
                         *FF8Addresses::pGameMode : 0xFFFF;

    s_tutoCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Dialog("FieldDialog: [TUTO] call#%d field=%s tutoId=%u->%u mode=%u->%u",
               s_tutoCallCount, fieldName,
               (unsigned)tutoIdBefore, (unsigned)tutoIdAfter,
               (unsigned)modeBefore, (unsigned)modeAfter);

    return result;
}

// ============================================================================
// v04.21: opcode_mesmode hook (0x106) -- sets message display mode.
// Mode 2 = borderless (used for thoughts). Fires BEFORE the text is shown.
// ============================================================================

static int __cdecl Hook_opcode_mesmode(int entityPtr)
{
    uint16_t modeBefore = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;
    int result = s_origMesmode(entityPtr);
    uint16_t modeAfter = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;

    s_mesmodeCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Dialog("FieldDialog: [MESMODE] call#%d field=%s mode=%u->%u entity=0x%08X",
               s_mesmodeCallCount, fieldName,
               (unsigned)modeBefore, (unsigned)modeAfter, (uint32_t)entityPtr);

    // Also scan windows -- mesmode often precedes the dialog text
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MESMODE");
    LeaveCriticalSection(&s_cs);

    return result;
}

// ============================================================================
// v04.21: opcode_ramesw hook (0x116) -- remote AMESW.
// One entity triggers auto-positioned dialog+wait on another entity.
// This might be how Quistis's script triggers Squall's thought text.
// ============================================================================

static int __cdecl Hook_opcode_ramesw(int entityPtr)
{
    uint16_t modeBefore = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;
    int result = s_origRamesw(entityPtr);
    uint16_t modeAfter = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;

    s_rameswCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Dialog("FieldDialog: [RAMESW] call#%d field=%s mode=%u->%u entity=0x%08X",
               s_rameswCallCount, fieldName,
               (unsigned)modeBefore, (unsigned)modeAfter, (uint32_t)entityPtr);

    // Scan windows after -- the remote dialog text should now be loaded
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("RAMESW");
    LeaveCriticalSection(&s_cs);

    return result;
}

// ============================================================================
// Detour handlers -- the six primary message opcodes
// ============================================================================

static int __cdecl Hook_opcode_mes(int entityPtr)
{
    int result = s_origMes(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MES");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_mesw(int entityPtr)
{
    int result = s_origMesw(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MESW");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_ask(int entityPtr)
{
    int result = s_origAsk(entityPtr);

    // v0.15.6.1: post-ASK slot+0x08 override.
    //
    // FFNx's replace_call pattern rewrote the engine's internal CALL
    // field_get_dialog_string operand to point at FFNx's own function, so
    // the v0.15.6 pre-fetch override (via Hook_field_get_dialog_string) is
    // bypassed (the v0.15.6 BAT log showed zero [GETSTR-RAW] lines despite
    // unconditional first-10-calls logging). DialogInject's flag is set
    // before opcode_ask and cleared after, so by the time we reach this
    // point, IsOverrideActive() is still true if our injected call is in
    // flight.
    //
    // s_origAsk has just populated slot+0x08 with the natural text pointer.
    // We overwrite it with the override buffer pointer. The next things
    // that read slot+0x08 are:
    //   - ScanAndSpeakChoiceWindows below (TTS path) -- decodes our text
    //   - the engine's render loop next frame -- displays our text
    //   - the engine's input handler -- positions cursor on our lines
    // firstQ/lastQ at slot+0x29/+0x2A were set from our opcode_ask args, so
    // cursor positions match our line layout (BAT log confirmed firstQ=1
    // lastQ=3 post-call).
    if (::DialogInject::IsOverrideActive()) {
        const char* overrideText = ::DialogInject::GetOverrideText();
        int targetSlot = ::DialogInject::GetOverrideSlot();
        if (overrideText && targetSlot >= 0 && targetSlot < MAX_WINDOWS) {
            uint8_t* winObj = GetWindowObj(targetSlot);
            if (winObj) {
                __try {
                    char** text1Ptr = (char**)(winObj + WIN_OBJ_TEXT1_OFFSET);
                    char* origText1 = *text1Ptr;
                    *text1Ptr = (char*)overrideText;
                    Log::Dialog("FieldDialog: [POST-ASK-OVERRIDE] Patched slot[%d]+0x08: 0x%08X -> 0x%08X",
                                targetSlot,
                                (uint32_t)(uintptr_t)origText1,
                                (uint32_t)(uintptr_t)overrideText);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log::Dialog("FieldDialog: [POST-ASK-OVERRIDE] SEH writing slot[%d]+0x08", targetSlot);
                }
            }
        }
    }

    EnterCriticalSection(&s_cs);
    ScanAndSpeakChoiceWindows("ASK");
    LeaveCriticalSection(&s_cs);
    ::ChaseDiag::OnAskOpcodeFired("ASK");  // v0.15.1: chase-diag template snapshot
    return result;
}

static int __cdecl Hook_opcode_ames(int entityPtr)
{
    int result = s_origAmes(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("AMES");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_aask(int entityPtr)
{
    int result = s_origAask(entityPtr);

    EnterCriticalSection(&s_cs);
    ScanAndSpeakChoiceWindows("AASK");
    LeaveCriticalSection(&s_cs);
    ::ChaseDiag::OnAskOpcodeFired("AASK");  // v0.15.1: chase-diag template snapshot
    return result;
}

static int __cdecl Hook_opcode_amesw(int entityPtr)
{
    int result = s_origAmesw(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("AMESW");
    LeaveCriticalSection(&s_cs);
    return result;
}

// ============================================================================
// v04.25: Repeat last dialog (F5 hotkey)
// ============================================================================

void RepeatLastDialog()
{
    if (s_lastDialogSpoken.empty()) {
        ScreenReader::Speak("No dialog to repeat.", true);
        return;
    }
    Log::Dialog("FieldDialog: [REPEAT] \"%s\"", s_lastDialogSpoken.c_str());
    ScreenReader::Speak(s_lastDialogSpoken.c_str(), true);  // Interrupt current speech
}
