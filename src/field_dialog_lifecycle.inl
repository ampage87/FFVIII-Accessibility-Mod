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
// v0.18.3.4: Timber-train uncoupling-code announcement (#56).
//
// The v0.18.3.3 [CODEVAR] probe confirmed the four code digits are bytes at
// field-varblock 0x1CFE9B8 + 1026..1029 (values 1-4) -- the only addressing
// that read four 1-4 values cycling on the code's ~5s refresh. Keykantoku
// writes them; Angoyarukun displays them left-to-right (1026 = leftmost).
//
// Read the four bytes each poll on a `tiagit*` field. When a NEW code settles
// (the same quad held for a few consecutive polls -- enough to skip the
// sub-second mixed states while the script rewrites the four bytes one at a
// time), speak it as four words ("Code. four, three, three, one."). Speaks
// once per settled code. SEH-guarded; reuses ScreenReader::Speak. Logs
// [TRAINCODE-SAY] to ff8_field.log.
// ============================================================================
static void TrainCodeAnnounce()
{
    static int s_lastSeen  = -1;   // last quad read, for the settle debounce
    static int s_settle    = 0;    // consecutive polls the current quad has held
    static int s_announced = -1;   // last quad actually spoken
    static const int SETTLE_POLLS = 5;  // ~500ms; skips the real train's noisier mid-rewrite transients

    if (!FF8Addresses::IsOnField()) { s_lastSeen = -1; s_settle = 0; s_announced = -1; return; }
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn) { s_lastSeen = -1; s_settle = 0; s_announced = -1; return; }
    // Code-apparatus fields, each with its own varblock location for the four
    // 1-4 digits (FFNx field_vars_stack base 0x1CFE9B8, read left-to-right):
    //   tiagit* (briefing-room practice model) -> bytes 1026..1029 (v0.18.3.3,
    //     entry-confirmed working on the practice panel).
    //   tilink1 (the real moving-train code panel) -> bytes 1029..1032
    //     (v0.18.3.6 [TRAINWIN]: 1029-1032 held a stable 1-4 code ~5s then
    //      cycled -- "2421"/"2433"/"4122" -- while 1026-1029 read [1 1 1 x];
    //      same apparatus, varblock shifted +3 on this field).
    unsigned codeBase;
    if      (_strnicmp(fn, "tiagit", 6) == 0) codeBase = 1026;
    else if (_strnicmp(fn, "tilink", 6) == 0) codeBase = 1029;
    else { s_lastSeen = -1; s_settle = 0; s_announced = -1; return; }

    int d[4] = {};
    bool okRead = true;
    __try {
        for (int i = 0; i < 4; i++) d[i] = *(uint8_t*)(0x1CFE9B8 + codeBase + i);
    } __except (EXCEPTION_EXECUTE_HANDLER) { okRead = false; }
    if (!okRead) return;

    // Valid displayed code = all four digits in 1..4. Anything else means the
    // code panel isn't currently presenting a code; reset the settle counter.
    for (int i = 0; i < 4; i++) {
        if (d[i] < 1 || d[i] > 4) { s_lastSeen = -1; s_settle = 0; return; }
    }

    int code = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];
    if (code == s_lastSeen) {
        if (s_settle < 1000) s_settle++;
    } else {
        s_lastSeen = code;
        s_settle   = 1;
    }
    // Fire exactly once, the poll the quad first becomes stable.
    if (s_settle != SETTLE_POLLS) return;
    if (code == s_announced) return;   // unchanged since the last announcement
    s_announced = code;

    static const char* kWord[5] = { "", "one", "two", "three", "four" };
    char msg[64];
    sprintf_s(msg, sizeof(msg), "Code. %s, %s, %s, %s.",
              kWord[d[0]], kWord[d[1]], kWord[d[2]], kWord[d[3]]);
    // Assertive (aria-live): interrupt=true so the code jumps the queue ahead of
    // any in-progress field TTS. The displayed code is time-sensitive (it cycles
    // ~every 5s and must be entered before it changes), so it must not wait.
    ScreenReader::Speak(msg, true);
    Log::Field("FieldDialog: [TRAINCODE-SAY] %s (%04d)", msg, code);
}

// ============================================================================
// v0.18.3.27: Timber code-entry key-layout announce (#60 / #57).
//
// During train code entry the four digits are spoken as NUMBERS (1-4), but a
// blind player has no way to discover which keys those numbers map to. "/"
// (VK_OEM_2) announces the layout whenever the player is on a code-apparatus
// field -- tiagit* (briefing-room practice) or tilink* (the live moving train).
// No conflict with the menu help bar that also uses "/": that handler in
// menu_tts.cpp is gated to menu mode (game mode 6) and never fires on the
// field, where code entry happens.
//
// Number-first framing ("1 is D, 2 is X...") matches the order the code is read
// aloud, so the player hears "two" and looks up 2 -> X directly. Spoken once
// per "/" press (edge-detected on the 0x8000 down bit). No memory reads, so no
// SEH needed; reuses ScreenReader::Speak. Logs [TRAINCODE-KEYS].
//
// #57 key map: 1 = Right / D, 2 = Down / X, 3 = Left / A, 4 = Up / W, Q = quit.
// We surface the D/X/A/W letters (Aaron's chosen framing); the arrow-key
// equivalents and Q-to-quit are omitted to keep the announce short.
// ============================================================================
static void TrainCodeKeyHelp()
{
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn) return;
    if (_strnicmp(fn, "tiagit", 6) != 0 && _strnicmp(fn, "tilink", 6) != 0) return;

    static bool s_slashWasDown = false;
    bool slash = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    if (slash && !s_slashWasDown) {
        ScreenReader::Speak("Code entry keys. 1 is D, 2 is X, 3 is A, 4 is W.", true);
        Log::Field("FieldDialog: [TRAINCODE-KEYS] announced D/X/A/W = 1/2/3/4 on %s", fn);
    }
    s_slashWasDown = slash;
}

// ============================================================================
// v0.18.3.13: Timber guard/round state runtime logger (#58).
//
// The v0.18.3.12 static dump mapped the minigame state machine: the patrol
// guards (blind2/blind3) walk on rails gated by field var 1040; the round is
// gated by var 1042; blind4 init zeroes the entered-code slots 1024-1027 and
// drives 1028 (round counter), 1038/1039 (display state), 1043 (code-entry),
// 1044 (down/up). Two unknowns block the suppression build: (1) Manual needs
// the value of var 1040 that = "guards idle/frozen"; (2) Auto/Skip needs to
// see the success/fail flag transitions. This logs the live byte values of
// those field vars (FFNx field_vars_stack 0x1CFE9B8 + idx, same addressing as
// the code digits) once per ~500ms on tilink* so a real run -- enter the code,
// then (a) get caught, (b) succeed/uncouple -- shows how each var moves at the
// catch and at the win. Pairs with [GUARDPOS] (still on) to correlate guard
// distance with var state. SEH-guarded, log-only, no writes -> [GUARDVAR].
// ============================================================================
#ifndef GUARD_VAR_DIAG
#define GUARD_VAR_DIAG 0   // OFF v0.18.3.20: #58 guard mechanic mapped + Original/Manual BAT-confirmed; silences the [GUARDVAR] flood AND the [GUARDFREEZE] confirmation below (the Manual var-pin itself stays active). Set to 1 to re-enable both for future guard-var investigation.
#endif
static void GuardVarLog()
{
#if GUARD_VAR_DIAG
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn || _strnicmp(fn, "tilink", 6) != 0) return;

    static DWORD s_lastLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastLog < 500) return;
    s_lastLog = now;

    int guard = 0, round = 0, v1038 = 0, v1039 = 0, v1028 = 0, v1043 = 0, v1044 = 0;
    int ent[4] = {}, code[4] = {};
    bool ok = true;
    __try {
        const uint8_t* base = (const uint8_t*)0x1CFE9B8;
        guard = base[1040];
        round = base[1042];
        v1038 = base[1038];
        v1039 = base[1039];
        v1028 = base[1028];
        v1043 = base[1043];
        v1044 = base[1044];
        for (int i = 0; i < 4; i++) ent[i]  = base[1024 + i];
        for (int i = 0; i < 4; i++) code[i] = base[1029 + i];
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return;

    Log::Field("FieldNavigation: [GUARDVAR] guard(1040)=%d round(1042)=%d "
               "1038=%d 1039=%d cnt(1028)=%d entry(1043)=%d updown(1044)=%d "
               "entered[1024-27]=%d,%d,%d,%d code[1029-32]=%d,%d,%d,%d",
               guard, round, v1038, v1039, v1028, v1043, v1044,
               ent[0], ent[1], ent[2], ent[3],
               code[0], code[1], code[2], code[3]);
#endif
}

// ============================================================================
// v0.18.3.14: Timber train-hijack guard MODE -- first real accessibility mode
// (#58). Three modes persist in ff8_accessibility.ini [Accessibility] under
// key `train_guard_mode`. v0.18.3.22 relabeled them to the user-facing scheme
// (NUMERIC VALUES UNCHANGED; default flipped to 0):
//   0 = Manual : guards move; code announce + per-guard proximity cues (the
//                accessible default -- a fully-vanilla mode strands a blind
//                player). Cue lives in field_nav_observe.inl (GuardManualCue).
//   1 = Freeze : guards FROZEN -- player enters the code at their own pace.
//   2 = Skip   : bypass the minigame (not yet implemented).
// Default = Manual (0). Mode is cached on first read so we don't touch the INI
// every poll.
//
// FREEZE implementation: pin the guard-patrol switch (field var 1040) to 0 on
// `tilink*` every poll. v0.18.3.13 [GUARDVAR] proved var 1040 = 1 while the
// guards patrol and 0 when idle; the patrol entities (blind2/blind3) read it
// and stop when it's 0. The code validators gate on var 1042 (round), NOT
// 1040, so code entry and the win still work with the guards frozen. The
// varblock is byte-indexed (one byte per var; confirmed by the #56 code
// digits at consecutive indices), so we write exactly the single byte at
// +1040 -- never a wider store that would clobber 1041/1042/1043.
// SEH-guarded; throttled [GUARDFREEZE] confirmation once per second.
// ============================================================================
// TGM_MANUAL/FREEZE/SKIP now live in field_dialog.h (enum TrainGuardModeVal)
// so FieldNavigation's Manual-mode cue can share them. The cache is a
// file-scope static (not a function-local) so SetTrainGuardMode() -- called by
// the in-engine mode ASK -- can update the live value without an INI round-trip.
static int s_trainGuardMode = -1;   // -1 = not yet loaded from INI

static int TrainGuardMode()
{
    if (s_trainGuardMode < 0) {
        Config::Load();
        s_trainGuardMode = Config::GetInt("train_guard_mode", TGM_MANUAL);  // TGM_MANUAL is now 0 = the default
        if (s_trainGuardMode < TGM_MANUAL || s_trainGuardMode > TGM_SKIP) s_trainGuardMode = TGM_MANUAL;
        // Write the resolved value straight back so the key always EXISTS in the
        // INI after first load -- otherwise GetInt's default is invisible and a
        // user (or the mode ASK, pre-launch) has no line to edit. Idempotent:
        // re-reading the same value and writing it back is a no-op on disk.
        Config::SetInt("train_guard_mode", s_trainGuardMode);
        Log::Field("FieldDialog: [TRAINMODE] train_guard_mode = %d (0=Manual,1=Freeze,2=Skip)", s_trainGuardMode);
    }
    return s_trainGuardMode;
}

// Public accessors (declared in field_dialog.h). GetTrainGuardMode lets
// FieldNavigation read the active mode for the Original proximity cue;
// SetTrainGuardMode lets the mode ASK update it and persist the choice.
int GetTrainGuardMode() { return TrainGuardMode(); }

void SetTrainGuardMode(int mode)
{
    if (mode < TGM_MANUAL || mode > TGM_SKIP) return;
    s_trainGuardMode = mode;
    Config::SetInt("train_guard_mode", mode);
    Log::Field("FieldDialog: [TRAINMODE] train_guard_mode set to %d (0=Manual,1=Freeze,2=Skip)", mode);
}

static void GuardFreezePin()
{
    if (TrainGuardMode() != TGM_FREEZE) return;   // formerly GuardManualFreeze; Freeze mode = old numeric 1
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn || _strnicmp(fn, "tilink", 6) != 0) return;

    __try {
        *(volatile uint8_t*)(0x1CFE9B8 + 1040) = 0;   // pin guard patrol switch OFF
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

#if GUARD_VAR_DIAG
    static DWORD s_lastLog = 0;
    DWORD now = GetTickCount();
    if (now - s_lastLog >= 1000) {
        s_lastLog = now;
        Log::Field("FieldDialog: [GUARDFREEZE] Manual mode -- pinned guard var 1040 = 0 on %s", fn);
    }
#endif
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
    TrainProbeDump();  // v0.18.3.0: train code-channel rule-out probe (#56)
    TrainCodeJsmDump();  // v0.18.3.2: dump code-apparatus JSM once on tiagit* entry (#56)
    TrainCodeVarProbe();  // v0.18.3.3: read code-digit varblock candidates on tiagit* (#56)
    TrainCodeAnnounce();  // v0.18.3.4: speak the 4 code digits on tiagit* when a new code settles (#56)
    TrainCodeKeyHelp();   // v0.18.3.27: "/" announces the D/X/A/W = 1/2/3/4 code-entry key layout (#60/#57)
    TrainFieldScan();     // v0.18.3.5: discover the real-train code field name + var location (#56)
    GuardJsmDump();       // v0.18.3.9: dump tilink1 guard + controller scripts once on entry (#58)
    GuardVarLog();        // v0.18.3.13: log tilink1 guard/round vars at runtime (#58)
    GuardFreezePin();     // v0.18.3.14: Freeze mode -- pin guard var 1040=0 to freeze guards (#58)

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
