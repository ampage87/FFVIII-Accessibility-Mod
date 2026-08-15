// field_nav_battlepause.inl — v0.18.3.236 (#72): battle pause/resume for F9 auto-drive
// Included from field_navigation.cpp immediately before Update(). Do not compile
// independently. Part of the FieldNavigation namespace; shares the file-scope
// statics declared in field_navigation.cpp (s_battlePause*, s_driveResumeRequest)
// and the helpers from earlier .inl includes (StopAutoDrive, RefreshCatalog,
// GetEntityPos, s_catalog/s_catalogCount, s_chaseDriveActive).
//
// Split out at creation time: adding this block inline pushed field_navigation.cpp
// to 84.3 KB, over the 80 KB CI hard cap.

// Battle pause/resume poll. Runs every Update() tick, INCLUDING while the game
// mode is battle (called before Update()'s IsOnField early-return). Two duties:
//   1. Battle-entry edge: an active F9 drive is stopped immediately, which
//      removes the fake gamepad and releases held keys so the battle menus
//      see real keyboard input (Aaron's 2026-07-12 Fire Cavern run: the held
//      steer state masked real arrows for the whole battle — only the held
//      direction appeared to work). The catalog target identity is saved.
//   2. Post-battle resume: once the game is back on the SAME field and the
//      player position has been readable for ~1 s, refresh the catalog,
//      re-select the saved target and arm a one-shot resume request that
//      HandleKeys consumes as a synthetic backslash press (so the resume
//      takes the exact same start path, validation included).
// Chase auto-pilot drives are exempt (chase has its own battle machinery).
// SEH note: plain data types only in this function (no C++ unwinding).
static void PollBattlePauseResume()
{
    // v0.20.106 (#minigame-bgbtl): the Garden-battle briefing patches a RET
    // over field_main to pause the game. GardenBattle::Update() -- which owns
    // the un-pause -- sits behind Update()'s on-field early-returns, so the
    // watchdog is called from HERE, above them. No-op unless frozen.
    GardenBattle::FreezeWatchdog();

    // Live battle mode value is 3 (see chase_detector.cpp MODE_BATTLE_VAL and
    // the 2026-07-12 log edges "game-mode 0x0001 -> 0x0003").
    static const uint16_t MODE_BATTLE_LIVE = 3;

    uint16_t mode = 0xFFFF;
    if (!FF8Addresses::pGameMode) return;
    __try { mode = *FF8Addresses::pGameMode; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    if (mode == MODE_BATTLE_LIVE) {
        if (s_driveActive && !s_chaseDriveActive) {
            s_battlePauseFieldId = FF8Addresses::pCurrentFieldId
                                   ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            s_battlePauseTargetValid = false;
            if (s_selectedCatalogIdx >= 0 && s_selectedCatalogIdx < s_catalogCount) {
                s_battlePauseEntityIdx   = s_catalog[s_selectedCatalogIdx].entityIdx;
                s_battlePauseGatewayIdx  = s_catalog[s_selectedCatalogIdx].gatewayIdx;
                s_battlePauseTargetValid = true;
            }
            s_battlePausePending     = s_battlePauseTargetValid;
            s_battleResumeReadyTicks = 0;
            Log::Field("FieldNavigation: [drive] battle entered mid-drive -> pausing "
                       "(fieldId=0x%04X targetEnt=%d gw=%d resumeArmed=%d)",
                       (unsigned)s_battlePauseFieldId, s_battlePauseEntityIdx,
                       s_battlePauseGatewayIdx, s_battlePausePending ? 1 : 0);
            StopAutoDrive("Auto-drive paused for battle.");
        }
        return;
    }

    if (!s_battlePausePending) return;
    if (!FF8Addresses::IsOnField()) { s_battleResumeReadyTicks = 0; return; }
    if (!FF8Addresses::HasFieldStateArrays()) { s_battleResumeReadyTicks = 0; return; }

    uint16_t fid = FF8Addresses::pCurrentFieldId
                   ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (fid != s_battlePauseFieldId) {
        // Field changed across the battle (scripted transition) — the saved
        // target is meaningless here. Drop the resume silently.
        Log::Field("FieldNavigation: [drive] battle resume dropped: field changed "
                   "(paused on 0x%04X, now 0x%04X)",
                   (unsigned)s_battlePauseFieldId, (unsigned)fid);
        s_battlePausePending = false;
        return;
    }

    // Wait for the player entity + position to be readable for a settle
    // window (~1 s at the ~60 Hz Update tick) before re-issuing the drive.
    float px = 0, pz = 0;
    if (s_playerEntityIdx < 0 || !GetEntityPos(s_playerEntityIdx, px, pz)) {
        s_battleResumeReadyTicks = 0;
        return;
    }
    if (++s_battleResumeReadyTicks < 60) return;

    s_battlePausePending = false;
    RefreshCatalog();
    int match = -1;
    for (int c = 0; c < s_catalogCount; c++) {
        if (s_catalog[c].entityIdx == s_battlePauseEntityIdx &&
            s_catalog[c].gatewayIdx == s_battlePauseGatewayIdx) {
            match = c;
            break;
        }
    }
    if (match < 0) {
        Log::Field("FieldNavigation: [drive] battle resume: target ent=%d gw=%d "
                   "not found in refreshed catalog (%d entries) -- resume abandoned",
                   s_battlePauseEntityIdx, s_battlePauseGatewayIdx, s_catalogCount);
        ScreenReader::Speak("Auto-drive target lost after battle.");
        return;
    }
    s_selectedCatalogIdx = match;
    s_driveResumeRequest = true;
    Log::Field("FieldNavigation: [drive] battle resume: target ent=%d gw=%d found "
               "at catIdx=%d -- re-issuing drive",
               s_battlePauseEntityIdx, s_battlePauseGatewayIdx, match);
}
