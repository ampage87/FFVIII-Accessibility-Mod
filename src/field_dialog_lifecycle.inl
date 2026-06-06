// field_dialog_lifecycle.inl -- Initialize, Shutdown, PollWindows.
//
// Goes LAST in the .inl chain because it references every hook function
// (Initialize wires them up via MinHook) and PollWindows calls into
// DiagRawWindowDump, ScanAndSpeakAllWindows, and CheckPendingTexts.
//
// Initialize: installs all MinHook detours, logs which ones succeeded.
// Shutdown:   mirrors Initialize, disabling every hook that was installed.
// PollWindows: called from the accessibility thread every ~100ms; runs the
//   FMV-aware polling fallback that catches dialogs the opcode hooks miss.

// ============================================================================
// Public interface
// ============================================================================

bool Initialize()
{
    if (s_initialized) return true;

    Log::Dialog("FieldDialog: === Initializing field dialog hooks (v04.36) ===");

    InitializeCriticalSection(&s_cs);

    if (FF8Addresses::opcode_mes == 0) {
        Log::Dialog("FieldDialog: ERROR - opcode_mes not resolved.");
        return false;
    }

    Log::Dialog("FieldDialog: pWindowsArray = 0x%08X",
               (uint32_t)(uintptr_t)FF8Addresses::pWindowsArray);

    bool anySuccess = false;

    if (CreateDetourHook(FF8Addresses::opcode_mesw, Hook_opcode_mesw, &s_origMesw, "opcode_mesw"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_mes, Hook_opcode_mes, &s_origMes, "opcode_mes"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_ask, Hook_opcode_ask, &s_origAsk, "opcode_ask"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_ames, Hook_opcode_ames, &s_origAmes, "opcode_ames"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_aask, Hook_opcode_aask, &s_origAask, "opcode_aask"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_amesw, Hook_opcode_amesw, &s_origAmesw, "opcode_amesw"))
        anySuccess = true;

    // v04.16: Hook field_get_dialog_string (different signature than opcodes)
    if (FF8Addresses::field_get_dialog_string != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::field_get_dialog_string,
            (LPVOID)Hook_field_get_dialog_string,
            (LPVOID*)&s_origGetDialogString);
        if (st == MH_OK) {
            Log::Dialog("FieldDialog: Hooked field_get_dialog_string: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::field_get_dialog_string,
                       (uint32_t)(uintptr_t)s_origGetDialogString);
            anySuccess = true;
        } else {
            Log::Dialog("FieldDialog: FAILED to hook field_get_dialog_string (status=%d)", (int)st);
        }
    }

    // v04.18: Hook opcode_tuto (0x177) for diagnostic logging
    if (FF8Addresses::opcode_tuto != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_tuto, Hook_opcode_tuto, &s_origTuto, "opcode_tuto"))
            anySuccess = true;
    } else {
        Log::Dialog("FieldDialog: WARNING - opcode_tuto not resolved");
    }

    // v04.21: Hook opcode_mesmode (0x106) and opcode_ramesw (0x116)
    if (FF8Addresses::opcode_mesmode != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_mesmode, Hook_opcode_mesmode, &s_origMesmode, "opcode_mesmode"))
            anySuccess = true;
    } else {
        Log::Dialog("FieldDialog: WARNING - opcode_mesmode not resolved");
    }
    if (FF8Addresses::opcode_ramesw != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_ramesw, Hook_opcode_ramesw, &s_origRamesw, "opcode_ramesw"))
            anySuccess = true;
    } else {
        Log::Dialog("FieldDialog: WARNING - opcode_ramesw not resolved");
    }

    // v04.17: Hook show_dialog (universal text renderer) for MODE_TUTO
    if (FF8Addresses::show_dialog_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::show_dialog_addr,
            (LPVOID)Hook_show_dialog,
            (LPVOID*)&s_origShowDialog);
        if (st == MH_OK) {
            Log::Dialog("FieldDialog: Hooked show_dialog: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::show_dialog_addr,
                       (uint32_t)(uintptr_t)s_origShowDialog);
            anySuccess = true;
        } else {
            Log::Dialog("FieldDialog: FAILED to hook show_dialog (status=%d)", (int)st);
        }
    } else {
        Log::Dialog("FieldDialog: WARNING - show_dialog not resolved, TUTO/thoughts won't be caught");
    }

    // v04.20: Hook menu_draw_text (naked counter for call-rate diagnostic)
    if (FF8Addresses::menu_draw_text_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::menu_draw_text_addr,
            (LPVOID)Hook_menu_draw_text_naked,
            (LPVOID*)&s_origMenuDrawText_raw);
        if (st == MH_OK) {
            Log::Dialog("FieldDialog: Hooked menu_draw_text: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::menu_draw_text_addr,
                       (uint32_t)(uintptr_t)s_origMenuDrawText_raw);
            anySuccess = true;
        } else {
            Log::Dialog("FieldDialog: FAILED to hook menu_draw_text (status=%d)", (int)st);
        }
    } else {
        Log::Dialog("FieldDialog: WARNING - menu_draw_text not resolved");
    }

    // v04.20: Hook get_character_width (per-glyph, accumulation-based text capture)
    if (FF8Addresses::get_character_width_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::get_character_width_addr,
            (LPVOID)Hook_get_character_width,
            (LPVOID*)&s_origGetCharWidth);
        if (st == MH_OK) {
            Log::Dialog("FieldDialog: Hooked get_character_width: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::get_character_width_addr,
                       (uint32_t)(uintptr_t)s_origGetCharWidth);
            anySuccess = true;
        } else {
            Log::Dialog("FieldDialog: FAILED to hook get_character_width (status=%d)", (int)st);
        }
    } else {
        Log::Dialog("FieldDialog: WARNING - get_character_width not resolved");
    }

    // v0.09.08: DISABLED update_field_entities hook for infirmary glitch diagnosis.
    // This naked hook intercepts the script interpreter entry point.
    Log::Dialog("FieldDialog: [DIAG] update_field_entities hook DISABLED for infirmary glitch test");

    // v0.09.04: Restored menuname hook (v04.35 style, minus enableGF)
    if (FF8Addresses::opcode_menuname != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_menuname, Hook_opcode_menuname, &s_origMenuname, "opcode_menuname"))
            anySuccess = true;
    } else {
        Log::Dialog("FieldDialog: WARNING - opcode_menuname not resolved");
    }

    // v0.09.08: DISABLED dispatch patch + update_field_entities hook for infirmary glitch diagnosis.
    // These intercept EVERY script opcode execution -- prime suspects for NPC walk hang.
    // if (PatchDispatchSite()) { anySuccess = true; }
    Log::Write("FieldDialog: [DIAG] Dispatch patch DISABLED for infirmary glitch test");

    if (!anySuccess) {
        Log::Write("FieldDialog: ERROR - No hooks were installed.");
        return false;
    }

    for (int i = 0; i < MAX_WINDOWS; i++)
        s_winState[i].Reset();

    s_initialized = true;
    Log::Write("FieldDialog: === Initialization complete ===");
    return true;
}

// ============================================================================
// Polling fallback: called from accessibility thread every ~100ms
//
// Catches dialogs that bypass our hooked opcodes (e.g. Squall's internal
// thoughts, some NPC chatter). The hooks remain the primary low-latency
// path for most dialogs; this is a safety net.
//
// Thread safety: acquires s_cs to coordinate with hook callbacks.
// ============================================================================

void PollWindows()
{
    if (!s_initialized || !FF8Addresses::pWindowsArray) return;
    // v04.17: Also poll during MODE_TUTO (thoughts/tutorials)
    if (!FF8Addresses::IsOnField() && !FF8Addresses::IsOnTuto()) return;

    // v04.16: Suppress polling during and briefly after FMV playback
    // Prevents stale/garbled window text from being spoken during transitions
    bool movieNow = FF8Addresses::IsMoviePlaying();
    if (movieNow) {
        s_lastPollMoviePlaying = true;
        return;
    }
    if (s_lastPollMoviePlaying) {
        // FMV just ended -- capture current stale text as "already spoken"
        // so the poller doesn't re-announce it when suppression ends.
        s_lastPollMoviePlaying = false;
        s_movieEndTime = GetTickCount();
        EnterCriticalSection(&s_cs);
        for (int i = 0; i < MAX_WINDOWS; i++) {
            uint8_t* winObj = GetWindowObj(i);
            char* text1 = GetWinText1(winObj);
            if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00) {
                std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
                if (!decoded.empty()) {
                    s_winState[i].lastSpokenText = decoded;
                    s_winState[i].lastRawText = decoded;
                }
            }
        }
        LeaveCriticalSection(&s_cs);
        Log::Write("FieldDialog: [POLL] FMV ended, captured stale text, suppressing %ums + resnap %ums",
                   FMV_SUPPRESS_MS, POST_FMV_RESNAP_MS);
        return;
    }
    if (s_movieEndTime != 0) {
        DWORD elapsed = GetTickCount() - s_movieEndTime;
        if (elapsed < FMV_SUPPRESS_MS) {
            return;  // Still in post-FMV suppression window
        }
        // v04.18: After suppression, enter continuous re-snapshot period.
        // Start the timer on first entry into this phase.
        if (s_postFmvResnapEndTime == 0) {
            s_postFmvResnapEndTime = GetTickCount() + POST_FMV_RESNAP_MS;
            Log::Write("FieldDialog: [POLL] Post-FMV suppression ended, starting resnap period");
        }
        if (GetTickCount() < s_postFmvResnapEndTime) {
            // Still in re-snapshot period: capture current window text as
            // "already spoken" without speaking. This absorbs rapidly-changing
            // garbage that appears in window buffers during transitions.
            EnterCriticalSection(&s_cs);
            for (int i = 0; i < MAX_WINDOWS; i++) {
                uint8_t* winObj = GetWindowObj(i);
                char* text1 = GetWinText1(winObj);
                if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00) {
                    std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
                    if (!decoded.empty()) {
                        s_winState[i].lastSpokenText = decoded;
                        s_winState[i].lastRawText = decoded;
                    }
                }
            }
            // Also reset show_dialog per-window tracking to absorb pointer changes
            for (int i = 0; i < MAX_WINDOWS; i++) {
                s_sdLastTextPtr[i] = nullptr;
                s_sdLastHash[i] = 0;  // v04.23: reset hashes too
                s_sdLastDecoded[i].clear();
            }
            LeaveCriticalSection(&s_cs);
            return;
        }
        // Re-snapshot period ended -- clear FMV state, resume normal polling
        Log::Write("FieldDialog: [POLL] Post-FMV resnap period ended, resuming normal polling");
        s_movieEndTime = 0;
        s_postFmvResnapEndTime = 0;
    }

    DiagRawWindowDump();

    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("POLL");
    CheckPendingTexts();  // v04.16: Speak deferred getstr texts
    // v04.24: Disabled GCW speak -- was diagnostic from v04.20, now causes
    // garbled "-G'" speech from character naming screen menu glyphs.
    // CheckGcwBuffer();
    LeaveCriticalSection(&s_cs);
}

void Shutdown()
{
    if (!s_initialized) return;

    if (s_origMesw)  MH_DisableHook((LPVOID)FF8Addresses::opcode_mesw);
    if (s_origMes)   MH_DisableHook((LPVOID)FF8Addresses::opcode_mes);
    if (s_origAsk)   MH_DisableHook((LPVOID)FF8Addresses::opcode_ask);
    if (s_origAmes)  MH_DisableHook((LPVOID)FF8Addresses::opcode_ames);
    if (s_origAask)  MH_DisableHook((LPVOID)FF8Addresses::opcode_aask);
    if (s_origAmesw) MH_DisableHook((LPVOID)FF8Addresses::opcode_amesw);
    if (s_origGetDialogString) MH_DisableHook((LPVOID)FF8Addresses::field_get_dialog_string);
    if (s_origTuto)  MH_DisableHook((LPVOID)FF8Addresses::opcode_tuto);
    if (s_origMesmode) MH_DisableHook((LPVOID)FF8Addresses::opcode_mesmode);
    if (s_origRamesw)  MH_DisableHook((LPVOID)FF8Addresses::opcode_ramesw);
    if (s_origMenuname) MH_DisableHook((LPVOID)FF8Addresses::opcode_menuname);
    if (s_origShowDialog) MH_DisableHook((LPVOID)FF8Addresses::show_dialog_addr);
    if (s_origMenuDrawText_raw) MH_DisableHook((LPVOID)FF8Addresses::menu_draw_text_addr);
    if (s_origGetCharWidth) MH_DisableHook((LPVOID)FF8Addresses::get_character_width_addr);
    if (s_origUpdateFieldEntities_raw) MH_DisableHook((LPVOID)FF8Addresses::update_field_entities_addr);
    UnpatchDispatchSite();

    s_origMesw = s_origMes = s_origAsk = s_origAmes = s_origAask = s_origAmesw = nullptr;
    s_origGetDialogString = nullptr;
    s_origTuto = nullptr;
    s_origMesmode = nullptr;
    s_origRamesw = nullptr;
    s_origMenuname = nullptr;
    s_origShowDialog = nullptr;
    s_origMenuDrawText_raw = nullptr;
    s_origGetCharWidth = nullptr;
    s_origUpdateFieldEntities_raw = nullptr;

    EnterCriticalSection(&s_cs);
    for (int i = 0; i < MAX_WINDOWS; i++)
        s_winState[i].Reset();
    s_pendingCount = 0;
    s_lastGetstrText.clear();
    s_lastTutoText.clear();
    LeaveCriticalSection(&s_cs);

    DeleteCriticalSection(&s_cs);

    s_initialized = false;
    Log::Write("FieldDialog: Shutdown complete.");
}
